/*  SPDX-License-Identifier: GPL-2.0-or-later */
#include "AcetateBackend.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <exception>
#include <functional>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <string>
#include <vector>

#include <sndfile.h>
#include <wx/app.h>

#include "MockedPrefs.h"
#include "Internat.h"
#include "Prefs.h"
#include "RealtimeEffectList.h"
#include "ProjectHistory.h"
#include "Project.h"
#include "ProjectFileIO.h"
#include "AudioIO.h"
#include "BasicUI.h"
#include "ExportPlugin.h"
#include "ExportPluginRegistry.h"
#include "FileNames.h"
#include "SampleFormat.h"
#include "StretchingSequence.h"
#include "TempDirectory.h"
#include "UndoManager.h"
#include "ViewInfo.h"
#include "sqlite/SQLiteUtils.h"
#include "Tags.h"
#include "Track.h"
#include "WaveChannelUtilities.h"
#include "WaveTrack.h"
#include "wxFileNameWrapper.h"

// Matches the unit-test fixtures: PortAudio is only initialised so lib-audio-io
// static state is valid; no stream is opened by this slice.
#include <portaudio.h>

namespace {
// Audacity code reports errors and asks for progress dialogs through
// BasicUI::Services. This host has no dialogs yet, so errors are logged and
// recorded, progress dialogs do nothing, and CallAfter work is queued for the
// host's main-thread update tick.
class HeadlessServices final : public BasicUI::Services {
public:
   HeadlessServices() : mUiThread{ std::this_thread::get_id() } {}

   std::string TakeError()
   {
      std::lock_guard<std::mutex> lock{ mMutex };
      return std::exchange(mError, {});
   }
   void RunQueued()
   {
      for (;;) {
         BasicUI::Action action;
         {
            std::lock_guard<std::mutex> lock{ mMutex };
            if (mQueue.empty())
               return;
            action = std::move(mQueue.front());
            mQueue.pop_front();
         }
         action();
      }
   }

   void DoCallAfter(const BasicUI::Action &action) override
   {
      std::lock_guard<std::mutex> lock{ mMutex };
      mQueue.push_back(action);
   }
   void DoYield() override {}
   void DoProcessIdle() override {}
   void DoShowErrorDialog(const BasicUI::WindowPlacement &,
      const TranslatableString &title, const TranslatableString &message,
      const ManualPageID &, const BasicUI::ErrorDialogOptions &) override
   {
      Record(title.Translation(), message.Translation());
   }
   BasicUI::MessageBoxResult DoMessageBox(
      const TranslatableString &message, BasicUI::MessageBoxOptions) override
   {
      Record("Message", message.Translation());
      return BasicUI::MessageBoxResult::Ok;
   }
   std::unique_ptr<BasicUI::ProgressDialog> DoMakeProgress(
      const TranslatableString &, const TranslatableString &, unsigned,
      const TranslatableString &) override
   {
      struct Silent final : BasicUI::ProgressDialog {
         BasicUI::ProgressResult Poll(unsigned long long, unsigned long long,
            const TranslatableString &) override
         { return BasicUI::ProgressResult::Success; }
         void SetMessage(const TranslatableString &) override {}
         void SetDialogTitle(const TranslatableString &) override {}
         void Reinit() override {}
      };
      return std::make_unique<Silent>();
   }
   std::unique_ptr<BasicUI::GenericProgressDialog> DoMakeGenericProgress(
      const BasicUI::WindowPlacement &, const TranslatableString &,
      const TranslatableString &, int) override
   {
      struct Silent final : BasicUI::GenericProgressDialog {
         BasicUI::ProgressResult Pulse() override
         { return BasicUI::ProgressResult::Success; }
      };
      return std::make_unique<Silent>();
   }
   int DoMultiDialog(const TranslatableString &message,
      const TranslatableString &, const TranslatableStrings &,
      const ManualPageID &, const TranslatableString &, bool) override
   {
      Record("Choice", message.Translation());
      return 0;
   }
   bool DoOpenInDefaultBrowser(const wxString &) override { return false; }
   std::unique_ptr<BasicUI::WindowPlacement> DoFindFocus() override
   { return nullptr; }
   void DoSetFocus(const BasicUI::WindowPlacement &) override {}
   bool IsUsingRtlLayout() const override { return false; }
   bool IsUiThread() const override
   { return std::this_thread::get_id() == mUiThread; }

private:
   void Record(const wxString &title, const wxString &message)
   {
      std::fprintf(stderr, "[acetate-host] %s: %s\n",
         title.utf8_str().data(), message.utf8_str().data());
      std::lock_guard<std::mutex> lock{ mMutex };
      mError = message.utf8_str().data();
   }

   const std::thread::id mUiThread;
   std::mutex mMutex;
   std::deque<BasicUI::Action> mQueue;
   std::string mError;
};
}

struct AcetateBackend {
   // The headless spike keeps settings in memory instead of touching the
   // user's Audacity preferences.
   MockedPrefs prefs;
   bool paInitialized{ false };
   std::shared_ptr<AudacityProject> project;
   unsigned long long revision{ 0 };
   std::string error;
   // Duplicates of the tracks at the last save or open. Holding them keeps the
   // saved file's sample blocks alive while the user edits, as ProjectFileManager
   // does with mLastSavedTracks.
   std::shared_ptr<TrackList> lastSaved;
   std::string projectPath;
   std::filesystem::path tempDir;
   HeadlessServices services;
   BasicUI::Services *previousServices{};

   AcetateBackend()
   {
      // wxStandardPaths and file-name helpers used by project and export code
      // need an application object for platform traits. No window or event
      // loop is created.
      if (!wxAppConsole::GetInstance()) {
         static wxAppConsole sApp;
         wxAppConsole::SetInstance(&sApp);
      }
      previousServices = BasicUI::Install(&services);
      // Sample blocks need a writable scratch directory; the application
      // normally chooses one at start-up.
      std::error_code ec;
      tempDir = std::filesystem::temp_directory_path(ec) /
         ("acetate-host-" + std::to_string(reinterpret_cast<uintptr_t>(this)));
      std::filesystem::create_directories(tempDir, ec);
      const auto tempPath = wxString::FromUTF8(tempDir.string());
      TempDirectory::SetDefaultTempDir(tempPath);
      FileNames::UpdateDefaultPath(FileNames::Operation::Temp, tempPath);
      paInitialized = Pa_Initialize() == paNoError;
      // The bundled SQLite is built without auto-initialisation.
      audacity::sqlite::Initialize();
      AudioIO::Init();
      project = AudacityProject::Create();
      // A temporary database backs the sample blocks, as for a new project.
      ProjectFileIO::Get(*project).OpenProject();
      // Gives undo a base state, as ProjectManager does for a new project.
      ProjectHistory::Get(*project).InitialState();
      ExportPluginRegistry::Get().Initialize();
   }
   ~AcetateBackend()
   {
      if (auto *audioIO = AudioIO::Get())
         if (audioIO->IsBusy())
            audioIO->StopStream();
      if (lastSaved)
         lastSaved->Clear();
      lastSaved.reset();
      if (project) {
         // The application clears history before closing a project.
         UndoManager::Get(*project).ClearStates();
         ProjectFileIO::Get(*project).CloseProject();
      }
      project.reset();
      AudioIO::Deinit();
      if (paInitialized)
         Pa_Terminate();
      BasicUI::Install(previousServices);
      std::error_code ec;
      std::filesystem::remove_all(tempDir, ec);
   }

   WaveTrack *Track(int index) const
   {
      if (index < 0)
         return nullptr;
      int i = 0;
      for (auto *track : TrackList::Get(*project).Any<WaveTrack>())
         if (i++ == index)
            return track;
      return nullptr;
   }
};

int acetate_backend_abi_version(void) { return ACETATE_BACKEND_ABI_VERSION; }

AcetateBackend *acetate_backend_create(void)
{
   try {
      return new AcetateBackend;
   }
   catch (...) {
      return nullptr;
   }
}

void acetate_backend_destroy(AcetateBackend *backend)
{
   delete backend;
}

const char *acetate_backend_last_error(const AcetateBackend *backend)
{
   return backend ? backend->error.c_str() : "no backend";
}

unsigned long long acetate_backend_revision(const AcetateBackend *backend)
{
   return backend ? backend->revision : 0;
}

int acetate_backend_import(AcetateBackend *backend, const char *utf8Path)
{
   if (!backend || !utf8Path)
      return 1;
   backend->error.clear();
   try {
      SF_INFO info{};
      SNDFILE *file = sf_open(utf8Path, SFM_READ, &info);
      if (!file) {
         backend->error = sf_strerror(nullptr);
         return 2;
      }
      auto closeFile = std::unique_ptr<SNDFILE, int (*)(SNDFILE *)>(
         file, &sf_close);
      if (info.channels < 1 || info.channels > 2) {
         backend->error = "only mono and stereo files are supported";
         return 3;
      }
      const size_t channels = static_cast<size_t>(info.channels);

      auto &factory = WaveTrackFactory::Get(*backend->project);
      auto track = factory.Create(channels, floatSample, info.samplerate);

      constexpr size_t kFrames = 1 << 16;
      std::vector<float> interleaved(kFrames * channels);
      std::vector<std::vector<float>> planar(
         channels, std::vector<float>(kFrames));
      for (;;) {
         const auto read = sf_readf_float(file, interleaved.data(), kFrames);
         if (read <= 0)
            break;
         for (sf_count_t i = 0; i < read; ++i)
            for (size_t c = 0; c < channels; ++c)
               planar[c][i] = interleaved[i * channels + c];
         for (size_t c = 0; c < channels; ++c)
            track->Append(
               c, reinterpret_cast<constSamplePtr>(planar[c].data()),
               floatSample, static_cast<size_t>(read));
      }
      track->Flush();
      TrackList::Get(*backend->project).Add(track);
      ProjectHistory::Get(*backend->project).PushState(
         XO("Imported audio"), XO("Import"));
      ++backend->revision;
      return 0;
   }
   catch (const std::exception &e) {
      backend->error = e.what();
   }
   catch (...) {
      backend->error = "unexpected error while importing";
   }
   return 4;
}

int acetate_backend_track_count(const AcetateBackend *backend)
{
   if (!backend)
      return 0;
   return static_cast<int>(
      TrackList::Get(*backend->project).Any<WaveTrack>().size());
}

int acetate_backend_channel_count(const AcetateBackend *backend, int track)
{
   auto *wave = backend ? backend->Track(track) : nullptr;
   return wave ? static_cast<int>(wave->NChannels()) : 0;
}

double acetate_backend_track_rate(const AcetateBackend *backend, int track)
{
   auto *wave = backend ? backend->Track(track) : nullptr;
   return wave ? wave->GetRate() : 0.0;
}

double acetate_backend_track_start(const AcetateBackend *backend, int track)
{
   auto *wave = backend ? backend->Track(track) : nullptr;
   return wave ? wave->GetStartTime() : 0.0;
}

double acetate_backend_track_end(const AcetateBackend *backend, int track)
{
   auto *wave = backend ? backend->Track(track) : nullptr;
   return wave ? wave->GetEndTime() : 0.0;
}

double acetate_backend_project_end(const AcetateBackend *backend)
{
   return backend ? TrackList::Get(*backend->project).GetEndTime() : 0.0;
}

int acetate_backend_waveform(
   AcetateBackend *backend, int track, int channel, double t0, double t1,
   int columns, float *minMax)
{
   auto *wave = backend ? backend->Track(track) : nullptr;
   if (!wave || !minMax || columns <= 0 || !(t1 > t0) || channel < 0 ||
       static_cast<size_t>(channel) >= wave->NChannels())
      return -1;
   try {
      const auto &chan = *wave->GetChannel(static_cast<size_t>(channel));
      const double end = wave->GetEndTime();
      const double width = (t1 - t0) / columns;
      for (int i = 0; i < columns; ++i) {
         const double a = t0 + i * width;
         const double b = a + width;
         float lo = 0.f, hi = 0.f;
         if (b > wave->GetStartTime() && a < end) {
            const auto range = WaveChannelUtilities::GetMinMax(
               chan, std::max(a, wave->GetStartTime()), std::min(b, end),
               false);
            lo = range.first;
            hi = range.second;
         }
         minMax[2 * i] = lo;
         minMax[2 * i + 1] = hi;
      }
      return columns;
   }
   catch (const std::exception &e) {
      backend->error = e.what();
   }
   catch (...) {
      backend->error = "unexpected error reading waveform";
   }
   return -1;
}

int acetate_backend_play(AcetateBackend *backend, double t0, double t1)
{
   if (!backend)
      return 1;
   backend->error.clear();
   try {
      auto *audioIO = AudioIO::Get();
      if (!audioIO) {
         backend->error = "audio engine is not initialised";
         return 2;
      }
      acetate_backend_update(backend);
      if (audioIO->IsBusy()) {
         backend->error = "audio engine is busy";
         return 3;
      }
      auto &tracks = TrackList::Get(*backend->project);
      TransportSequences sequences;
      for (auto *track : tracks.Any<WaveTrack>())
         sequences.playbackSequences.push_back(
            StretchingSequence::Create(*track, track->GetClipInterfaces()));
      if (sequences.playbackSequences.empty()) {
         backend->error = "nothing to play";
         return 4;
      }
      const double end = tracks.GetEndTime();
      t0 = std::clamp(t0, 0.0, end);
      t1 = std::clamp(t1, 0.0, end);
      if (!(t1 > t0)) {
         backend->error = "empty play region";
         return 5;
      }
      AudioIOStartStreamOptions options{
         backend->project, (*tracks.Any<WaveTrack>().begin())->GetRate() };
      options.playNonWaveTracks = false;
      const int token = audioIO->StartStream(sequences, t0, t1, t1, options);
      if (token == 0) {
         backend->error = "the audio device could not start the stream";
         return 6;
      }
      return 0;
   }
   catch (const std::exception &e) {
      backend->error = e.what();
   }
   catch (...) {
      backend->error = "unexpected error starting playback";
   }
   return 7;
}

void acetate_backend_stop(AcetateBackend *backend)
{
   if (!backend)
      return;
   if (auto *audioIO = AudioIO::Get())
      if (audioIO->IsBusy())
         audioIO->StopStream();
}

void acetate_backend_update(AcetateBackend *backend)
{
   if (backend)
      backend->services.RunQueued();
   auto *audioIO = backend ? AudioIO::Get() : nullptr;
   if (audioIO && audioIO->IsBusy() && !audioIO->IsStreamActive())
      audioIO->StopStream();
}

int acetate_backend_is_playing(const AcetateBackend *backend)
{
   auto *audioIO = backend ? AudioIO::Get() : nullptr;
   return audioIO && audioIO->IsStreamActive() ? 1 : 0;
}

double acetate_backend_play_position(const AcetateBackend *backend)
{
   auto *audioIO = backend ? AudioIO::Get() : nullptr;
   if (!audioIO || !audioIO->IsStreamActive())
      return -1.0;
   return audioIO->GetStreamTime();
}

void acetate_backend_set_selection(
   AcetateBackend *backend, double t0, double t1)
{
   if (!backend)
      return;
   auto &region = ViewInfo::Get(*backend->project).selectedRegion;
   if (t1 < t0)
      std::swap(t0, t1);
   region.setTimes(std::max(0.0, t0), std::max(0.0, t1));
}

void acetate_backend_get_selection(
   const AcetateBackend *backend, double *t0, double *t1)
{
   double a = 0.0, b = 0.0;
   if (backend) {
      const auto &region = ViewInfo::Get(*backend->project).selectedRegion;
      a = region.t0();
      b = region.t1();
   }
   if (t0)
      *t0 = a;
   if (t1)
      *t1 = b;
}

namespace {
// Shared guard for edits: Audacity's menus are disabled while audio is busy.
bool CanEdit(AcetateBackend &backend)
{
   backend.error.clear();
   if (auto *audioIO = AudioIO::Get(); audioIO && audioIO->IsBusy()) {
      acetate_backend_update(&backend);
      if (audioIO->IsBusy()) {
         backend.error = "stop playback before editing";
         return false;
      }
   }
   return true;
}
}

int acetate_backend_delete_selection(AcetateBackend *backend)
{
   if (!backend)
      return 1;
   if (!CanEdit(*backend))
      return 2;
   try {
      auto &project = *backend->project;
      auto &region = ViewInfo::Get(project).selectedRegion;
      const double t0 = region.t0(), t1 = region.t1();
      if (!(t1 > t0)) {
         backend->error = "no time selected";
         return 3;
      }
      // Mirrors OnDelete in src/menus/EditMenus.cpp.
      for (auto *track : TrackList::Get(project).Any<WaveTrack>())
         track->Clear(t0, t1);
      const double seconds = region.duration();
      region.collapseToT0();
      ProjectHistory::Get(project).PushState(
         XO("Deleted %.2f seconds at t=%.2f").Format(seconds, region.t0()),
         XO("Delete"));
      ++backend->revision;
      return 0;
   }
   catch (const std::exception &e) {
      backend->error = e.what();
   }
   catch (...) {
      backend->error = "unexpected error while deleting";
   }
   return 4;
}

int acetate_backend_can_undo(const AcetateBackend *backend)
{
   return backend && ProjectHistory::Get(*backend->project).UndoAvailable();
}

int acetate_backend_can_redo(const AcetateBackend *backend)
{
   return backend && ProjectHistory::Get(*backend->project).RedoAvailable();
}

int acetate_backend_undo(AcetateBackend *backend)
{
   if (!backend)
      return 1;
   if (!CanEdit(*backend))
      return 2;
   auto &project = *backend->project;
   if (!ProjectHistory::Get(project).UndoAvailable()) {
      backend->error = "nothing to undo";
      return 3;
   }
   UndoManager::Get(project).Undo([&](const UndoStackElem &elem) {
      ProjectHistory::Get(project).PopState(elem.state);
   });
   ++backend->revision;
   return 0;
}

int acetate_backend_redo(AcetateBackend *backend)
{
   if (!backend)
      return 1;
   if (!CanEdit(*backend))
      return 2;
   auto &project = *backend->project;
   if (!ProjectHistory::Get(project).RedoAvailable()) {
      backend->error = "nothing to redo";
      return 3;
   }
   UndoManager::Get(project).Redo([&](const UndoStackElem &elem) {
      ProjectHistory::Get(project).PopState(elem.state);
   });
   ++backend->revision;
   return 0;
}

namespace {
std::shared_ptr<TrackList> SnapshotTracks(AudacityProject &project)
{
   auto snapshot = TrackList::Create(nullptr);
   for (auto *track : TrackList::Get(project))
      snapshot->Add(track->Duplicate(Track::DuplicateOptions{}.Backup()));
   return snapshot;
}

// Ports of ProjectFileManager::ReadProjectFile's "last saved" snapshot and of
// ProjectFileManager::FixTracks (src/ProjectFileManager.cpp), which sit in the
// wx-coupled application target and so cannot be linked here. Without
// FixTracks, stereo channels loaded as linked pairs are never zipped back into
// one two-channel track and the right channel is lost. These belong in a
// UI-free library; see docs/research/host-spike.md.
std::shared_ptr<TrackList> SnapshotLoaded(TrackList &tracks)
{
   // Duplicates keep an extra reference on the blocks on disk so that the
   // saved file's contents are preserved until the user saves again.
   auto snapshot = TrackList::Create(nullptr);
   WaveTrack *leader{};
   tracks.Any().Visit(
      [&](WaveTrack &track) {
         for (const auto pChannel : TrackList::Channels(&track)) {
            auto left = leader;
            auto newTrack =
               pChannel->Duplicate(Track::DuplicateOptions{}.Backup());
            leader = left ? nullptr
               : (pChannel->GetLinkType() == Track::LinkType::None) ? nullptr
               : static_cast<WaveTrack *>(newTrack.get());
            snapshot->Add(newTrack);
            if (left)
               left->ZipClips(false);
         }
      },
      [&](Track &track) {
         snapshot->Add(track.Duplicate(Track::DuplicateOptions{}.Backup()));
      });
   return snapshot;
}

// Returns false when a channel was missing or a track reported a read error.
bool FixTracks(TrackList &tracks)
{
   bool ok = true;
   Track::Holder unlinkedTrack;
   for (auto iter = tracks.begin(); iter != tracks.end();) {
      auto t = (*iter++)->SharedPointer();
      const auto linkType = t->GetLinkType();
      if (!t->LinkConsistencyFix()) {
         ok = false;
         unlinkedTrack = nullptr;
      }
      if (!unlinkedTrack) {
         if (linkType != ChannelGroup::LinkType::None && t->NChannels() == 1) {
            unlinkedTrack = t;
            iter = tracks.Find(t.get());
            ++iter;
         }
      }
      else {
         if (const auto left = dynamic_cast<WaveTrack *>(unlinkedTrack.get()))
            if (const auto right = dynamic_cast<WaveTrack *>(t.get())) {
               left->SetPan(-1.0f);
               right->SetPan(1.0f);
               RealtimeEffectList::Get(*left).Clear();
               RealtimeEffectList::Get(*right).Clear();
            }
         unlinkedTrack = nullptr;
      }
      if (t->GetErrorOpening())
         ok = false;
   }
   return ok;
}

void ClearProject(AudacityProject &project)
{
   UndoManager::Get(project).ClearStates();
   ProjectFileIO::Get(project).CloseProject();
}
}

int acetate_backend_save(AcetateBackend *backend, const char *utf8Path)
{
   if (!backend || !utf8Path)
      return 1;
   if (!CanEdit(*backend))
      return 2;
   try {
      auto &project = *backend->project;
      auto &io = ProjectFileIO::Get(project);
      const auto path = wxString::FromUTF8(utf8Path);
      if (!path.Lower().EndsWith(".aup3")) {
         backend->error = "project files must end in .aup3";
         return 3;
      }
      // Like Save As in the application: keep a backup of an existing target
      // until the save succeeds. Saving to the project's own open file must not
      // do this, since the backup renames the file the connection is using.
      std::optional<ProjectFileIO::BackupProject> backup;
      const bool sameFile = !io.IsTemporary() &&
         wxFileName(path).SameAs(wxFileName(io.GetFileName()));
      if (!sameFile && wxFileExists(path)) {
         backup.emplace(io, path);
         if (!backup->IsOk()) {
            backend->error = "could not back up the existing file";
            return 4;
         }
      }
      if (!io.SaveProject(path, backend->lastSaved.get())) {
         backend->error = backend->services.TakeError();
         if (backend->error.empty())
            backend->error = "Audacity could not save the project";
         return 5;
      }
      project.SetProjectName(wxFileName(path).GetName());
      io.SetProjectTitle();
      UndoManager::Get(project).StateSaved();
      if (backend->lastSaved)
         backend->lastSaved->Clear();
      backend->lastSaved = SnapshotTracks(project);
      backend->projectPath = utf8Path;
      if (backup)
         backup->Discard();
      return 0;
   }
   catch (const std::exception &e) {
      backend->error = e.what();
   }
   catch (...) {
      backend->error = "unexpected error while saving";
   }
   return 6;
}

int acetate_backend_open(AcetateBackend *backend, const char *utf8Path)
{
   if (!backend || !utf8Path)
      return 1;
   if (!CanEdit(*backend))
      return 2;
   try {
      const auto path = wxString::FromUTF8(utf8Path);
      // Do not point the database layer at arbitrary files.
      if (!path.Lower().EndsWith(".aup3")) {
         backend->error = "project files must end in .aup3";
         return 3;
      }
      if (!wxFileExists(path)) {
         backend->error = "the project file does not exist";
         return 3;
      }
      // Same sequence as opening a file in the application: a fresh project
      // with a temporary connection, then LoadProject over it.
      auto fresh = AudacityProject::Create();
      auto &io = ProjectFileIO::Get(*fresh);
      auto discard = [&] { ClearProject(*fresh); };
      if (!io.OpenProject()) {
         backend->error = "could not create a working project";
         return 3;
      }
      auto parsed = io.LoadProject(path, false);
      if (!parsed) {
         backend->error = backend->services.TakeError();
         if (backend->error.empty())
            backend->error = io.GetLastError().Translation().utf8_str().data();
         discard();
         return 4;
      }
      auto &loadedTracks = TrackList::Get(*fresh);
      auto snapshot = SnapshotLoaded(loadedTracks);
      if (!FixTracks(loadedTracks)) {
         backend->error = "the project file has damaged or missing tracks";
         snapshot->Clear();
         discard();
         return 6;
      }
      parsed->Commit();
      ProjectHistory::Get(*fresh).InitialState();

      // Success: retire the previous project.
      if (backend->lastSaved)
         backend->lastSaved->Clear();
      ClearProject(*backend->project);
      backend->project = std::move(fresh);
      backend->lastSaved = std::move(snapshot);
      backend->projectPath = utf8Path;
      ++backend->revision;
      return 0;
   }
   catch (const std::exception &e) {
      backend->error = e.what();
   }
   catch (...) {
      backend->error = "unexpected error while opening";
   }
   return 7;
}

int acetate_backend_export_wav(AcetateBackend *backend, const char *utf8Path)
{
   if (!backend || !utf8Path)
      return 1;
   if (!CanEdit(*backend))
      return 2;
   try {
      auto &project = *backend->project;
      auto &tracks = TrackList::Get(project);
      const double end = tracks.GetEndTime();
      if (!(end > 0.0)) {
         backend->error = "nothing to export";
         return 3;
      }
      unsigned channels = 1;
      double rate = 0.0;
      for (auto *track : tracks.Any<WaveTrack>()) {
         if (track->NChannels() > 1)
            channels = 2;
         rate = std::max(rate, track->GetRate());
      }
      auto [plugin, format] =
         ExportPluginRegistry::Get().FindFormat(wxT("WAV"));
      if (!plugin) {
         backend->error = "the WAV export plug-in is not available";
         return 4;
      }
      auto processor = plugin->CreateProcessor(format);
      wxFileNameWrapper fileName{ wxString::FromUTF8(utf8Path) };
      if (!processor->Initialize(
             project, {}, fileName, 0.0, end, false, rate, channels, nullptr,
             &Tags::Get(project))) {
         backend->error = backend->services.TakeError();
         if (backend->error.empty())
            backend->error = "export could not start";
         return 5;
      }
      struct Delegate final : ExportProcessorDelegate {
         bool IsCancelled() const override { return false; }
         bool IsStopped() const override { return false; }
         void SetStatusString(const TranslatableString &) override {}
         void OnProgress(double) override {}
      } delegate;
      if (processor->Process(delegate) != ExportResult::Success) {
         backend->error = backend->services.TakeError();
         if (backend->error.empty())
            backend->error = "export failed";
         return 6;
      }
      return 0;
   }
   catch (const std::exception &e) {
      backend->error = e.what();
   }
   catch (...) {
      backend->error = "unexpected error while exporting";
   }
   return 7;
}

int acetate_backend_has_unsaved_changes(const AcetateBackend *backend)
{
   return backend && UndoManager::Get(*backend->project).UnsavedChanges();
}

const char *acetate_backend_project_path(AcetateBackend *backend)
{
   return backend ? backend->projectPath.c_str() : "";
}

void acetate_backend_get_view(
   const AcetateBackend *backend, double *left, double *pixelsPerSecond)
{
   double a = 0.0, b = ZoomInfo::GetDefaultZoom();
   if (backend) {
      const auto &view = ViewInfo::Get(*backend->project);
      a = view.hpos;
      b = view.GetZoom();
   }
   if (left)
      *left = a;
   if (pixelsPerSecond)
      *pixelsPerSecond = b;
}

double acetate_backend_set_view(
   AcetateBackend *backend, double left, double pixelsPerSecond)
{
   if (!backend)
      return 0.0;
   auto &view = ViewInfo::Get(*backend->project);
   view.SetZoom(pixelsPerSecond);
   view.hpos = left;
   return view.GetZoom();
}

void acetate_backend_zoom_limits(double *minPps, double *maxPps)
{
   if (minPps)
      *minPps = ZoomInfo::GetMinZoom();
   if (maxPps)
      *maxPps = ZoomInfo::GetMaxZoom();
}
