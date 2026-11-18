/*  SPDX-License-Identifier: GPL-2.0-or-later */
#include "AcetateBackend.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <exception>
#include <functional>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <string>
#include <vector>

#if defined(_WIN32)
#   define NOMINMAX
#   include <windows.h>
#   include <process.h>
#else
#   include <fcntl.h>
#   include <sys/file.h>
#   include <unistd.h>
#endif

#include <sndfile.h>
#include <wx/app.h>

#include "SettingsWX.h"
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
#include <sqlite3.h>
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

namespace fs = std::filesystem;

// An exclusive lock that the operating system drops when the process dies, so a
// crashed host leaves its session directory unlocked. It is how a starting host
// tells a live session from one it may offer to recover. Untested on Windows.
class FileLock final {
public:
   FileLock() = default;
   FileLock(const FileLock &) = delete;
   FileLock &operator=(const FileLock &) = delete;
   FileLock(FileLock &&other) noexcept { Take(other); }
   FileLock &operator=(FileLock &&other) noexcept
   {
      if (this != &other) {
         Release();
         Take(other);
      }
      return *this;
   }
   ~FileLock() { Release(); }

   bool Held() const
   {
#if defined(_WIN32)
      return mHandle != INVALID_HANDLE_VALUE;
#else
      return mFd >= 0;
#endif
   }

   bool Acquire(const fs::path &path)
   {
      Release();
#if defined(_WIN32)
      // Share mode 0 refuses every other open while this handle exists.
      mHandle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
         nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
#else
      const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
      if (fd < 0)
         return false;
      if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
         ::close(fd);
         return false;
      }
      mFd = fd;
#endif
      return Held();
   }

   void Release()
   {
#if defined(_WIN32)
      if (mHandle != INVALID_HANDLE_VALUE)
         CloseHandle(mHandle);
      mHandle = INVALID_HANDLE_VALUE;
#else
      if (mFd >= 0)
         ::close(mFd);   // closing drops the flock
      mFd = -1;
#endif
   }

private:
   void Take(FileLock &other)
   {
#if defined(_WIN32)
      mHandle = std::exchange(other.mHandle, INVALID_HANDLE_VALUE);
#else
      mFd = std::exchange(other.mFd, -1);
#endif
   }
#if defined(_WIN32)
   HANDLE mHandle{ INVALID_HANDLE_VALUE };
#else
   int mFd{ -1 };
#endif
};

// The host's own data: preferences and per-session working directories.
fs::path HostDataDir()
{
   if (const char *override = std::getenv("ACETATE_HOST_DATA"))
      if (*override)
         return fs::path{ override };
#if defined(_WIN32)
   if (const wchar_t *appData = _wgetenv(L"APPDATA"))
      return fs::path{ appData } / L"Acetate Host";
#elif defined(__APPLE__)
   if (const char *home = std::getenv("HOME"))
      return fs::path{ home } / "Library" / "Application Support" / "Acetate Host";
#else
   if (const char *config = std::getenv("XDG_CONFIG_HOME"))
      if (*config)
         return fs::path{ config } / "acetate-host";
   if (const char *home = std::getenv("HOME"))
      return fs::path{ home } / ".config" / "acetate-host";
#endif
   std::error_code ec;
   return fs::temp_directory_path(ec) / "acetate-host-data";
}

constexpr const char *kProjectExtension = ".aup3unsaved";

// The working project Audacity creates for a session, if the directory has one.
fs::path FindSessionProject(const fs::path &dir)
{
   std::error_code ec;
   for (fs::directory_iterator it{ dir, ec }, end; !ec && it != end; it.increment(ec))
      if (it->is_regular_file(ec) && it->path().extension() == kProjectExtension)
         return it->path();
   return {};
}

// Whether a session's project file holds an autosave document, that is, any
// state worth recovering: a host that was only started and killed leaves an
// empty working project. An unreadable file counts as content, because losing
// something recoverable is worse than offering something empty.
bool SessionHasContent(const fs::path &file)
{
   sqlite3 *db = nullptr;
   if (sqlite3_open_v2(file.string().c_str(), &db, SQLITE_OPEN_READWRITE, nullptr)
       != SQLITE_OK) {
      sqlite3_close(db);
      return true;
   }
   bool content = true;
   sqlite3_stmt *stmt = nullptr;
   if (sqlite3_prepare_v2(db, "SELECT COUNT(1) FROM autosave;", -1, &stmt,
          nullptr) == SQLITE_OK) {
      if (sqlite3_step(stmt) == SQLITE_ROW)
         content = sqlite3_column_int(stmt, 0) > 0;
   }
   sqlite3_finalize(stmt);
   sqlite3_close(db);
   return content;
}

// Removes a session directory once it no longer holds a project.
void RemoveSessionIfEmpty(const fs::path &dir)
{
   if (dir.empty() || !FindSessionProject(dir).empty())
      return;
   std::error_code ec;
   fs::remove_all(dir, ec);
}

struct Recoverable {
   fs::path dir;
   fs::path file;
   double modified{};
   double bytes{};
};

// Audacity's preference code reads and writes through gPrefs; here it is a
// file in the host's data directory rather than the wx app's audacity.cfg.
// SettingsWX is Audacity's own wxFileConfig-backed implementation.
struct HostPrefs {
   explicit HostPrefs(const fs::path &file)
   {
      InitPreferences(std::make_unique<SettingsWX>(
         wxString::FromUTF8(file.string())));
   }
   ~HostPrefs() { FinishPreferences(); }
   HostPrefs(const HostPrefs &) = delete;
   HostPrefs &operator=(const HostPrefs &) = delete;
};

wxString HostKey(const char *key)
{
   return wxString{ "/AcetateHost/" } + wxString::FromUTF8(key);
}
}

struct AcetateBackend {
   fs::path dataDir;
   fs::path sessionsDir;
   std::optional<HostPrefs> prefs;
   bool paInitialized{ false };
   std::shared_ptr<AudacityProject> project;
   unsigned long long revision{ 0 };
   std::string error;
   // Duplicates of the tracks at the last save or open. Holding them keeps the
   // saved file's sample blocks alive while the user edits, as ProjectFileManager
   // does with mLastSavedTracks.
   std::shared_ptr<TrackList> lastSaved;
   std::string projectPath;
   // This process's working directory, locked for its lifetime.
   fs::path sessionDir;
   FileLock sessionLock;
   // Sessions left by earlier processes, found at start-up.
   std::vector<Recoverable> recoverables;
   // A recovered session whose project is still the open one.
   fs::path claimDir;
   FileLock claimLock;
   bool recovered{ false };
   std::string prefString;
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
      std::error_code ec;
      dataDir = HostDataDir();
      sessionsDir = dataDir / "sessions";
      fs::create_directories(sessionsDir, ec);
      prefs.emplace(dataDir / "preferences.cfg");
      // The bundled SQLite is built without auto-initialisation, and the scan
      // below opens project files.
      audacity::sqlite::Initialize();
      ScanSessions();
      // Sample blocks and the working project need a writable scratch
      // directory; the application normally chooses one at start-up. Each host
      // gets its own, locked while it runs. Another host may be creating its
      // directory at the same moment and sweep ours as an empty leftover, so
      // creation and locking are retried.
      for (int attempt = 0; attempt < 3 && !sessionLock.Held(); ++attempt) {
         sessionDir = sessionsDir / ("session-" + SessionStamp());
         fs::create_directories(sessionDir, ec);
         sessionLock.Acquire(sessionDir / "lock");
      }
      if (!sessionLock.Held())
         throw std::runtime_error("could not create a session directory");
      const auto tempPath = wxString::FromUTF8(sessionDir.string());
      TempDirectory::SetDefaultTempDir(tempPath);
      FileNames::UpdateDefaultPath(FileNames::Operation::Temp, tempPath);
      // The directory is also stored in the preferences file, and
      // TempDirectory caches what it read; make it read ours.
      TempDirectory::ResetTempDir();
      paInitialized = Pa_Initialize() == paNoError;
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
         auto &io = ProjectFileIO::Get(*project);
         auto &undo = UndoManager::Get(*project);
         // A never-saved project with edits beyond its import is kept for
         // recovery instead of being deleted with the session: there is no
         // quit confirmation yet, so this is the only thing between a stray
         // quit and lost work. CloseProject removes a temporary project only
         // when it sits in the temp directory, so point that elsewhere first.
         const bool keep = io.IsTemporary() && undo.UnsavedChanges() &&
            undo.GetNumStates() > 2;
         // The application clears history before closing a project.
         undo.ClearStates();
         if (keep) {
            FileNames::UpdateDefaultPath(FileNames::Operation::Temp,
               wxString::FromUTF8((dataDir / "retained").string()));
            TempDirectory::ResetTempDir();
         }
         io.CloseProject();
         if (keep) {
            std::error_code ec;
            fs::remove(dataDir / "retained", ec);   // only if empty
         }
      }
      project.reset();
      AudioIO::Deinit();
      if (paInitialized)
         Pa_Terminate();
      BasicUI::Install(previousServices);
      // Locks drop first so the directories can be removed on every platform.
      sessionLock.Release();
      RemoveSessionIfEmpty(sessionDir);
      ReleaseClaim();
   }

   static std::string SessionStamp()
   {
      const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
         std::chrono::system_clock::now().time_since_epoch()).count();
#if defined(_WIN32)
      const auto pid = static_cast<long>(_getpid());
#else
      const auto pid = static_cast<long>(::getpid());
#endif
      return std::to_string(now) + "-" + std::to_string(pid);
   }

   // Lists session directories left by earlier processes: unlocked, and holding
   // a project. Unlocked directories without one are leftovers and are removed.
   void ScanSessions()
   {
      recoverables.clear();
      std::error_code ec;
      for (fs::directory_iterator it{ sessionsDir, ec }, end; !ec && it != end;
           it.increment(ec)) {
         const auto dir = it->path();
         if (!it->is_directory(ec))
            continue;
         FileLock probe;
         if (!probe.Acquire(dir / "lock"))
            continue;   // a running host, or one that is being recovered
         const auto file = FindSessionProject(dir);
         if (file.empty() || !SessionHasContent(file)) {
            // Nothing to recover: a leftover directory, or a host that was
            // started and killed before any state was recorded.
            probe.Release();
            std::error_code removeError;
            fs::remove_all(dir, removeError);
            continue;
         }
         Recoverable entry;
         entry.dir = dir;
         entry.file = file;
         const auto written = fs::last_write_time(file, ec);
         if (!ec) {
            const auto age = fs::file_time_type::clock::now() - written;
            entry.modified = std::chrono::duration<double>(
               (std::chrono::system_clock::now() - age).time_since_epoch())
               .count();
         }
         entry.bytes = static_cast<double>(fs::file_size(file, ec));
         recoverables.push_back(std::move(entry));
      }
      std::sort(recoverables.begin(), recoverables.end(),
         [](const Recoverable &a, const Recoverable &b) {
            return a.modified > b.modified;
         });
   }

   void ReleaseClaim()
   {
      claimLock.Release();
      if (!claimDir.empty())
         RemoveSessionIfEmpty(claimDir);
      claimDir.clear();
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

// Replaces the current project with the file at `path`, as opening a file does
// in the application: a fresh project with a temporary connection, then
// LoadProject over it. Whether LoadProject honours an autosave document is
// Audacity's own behaviour, and a project loaded from one is marked modified
// by a "Project was recovered" state, as in ProjectFileManager::OpenFile. On
// failure the current project is kept. Returns 0 or a non-zero status with
// backend.error set.
int LoadProjectFile(AcetateBackend &backend, const wxString &path)
{
   auto fresh = AudacityProject::Create();
   auto &io = ProjectFileIO::Get(*fresh);
   auto discard = [&] { ClearProject(*fresh); };
   if (!io.OpenProject()) {
      backend.error = "could not create a working project";
      return 3;
   }
   auto parsed = io.LoadProject(path, false);
   if (!parsed) {
      backend.error = backend.services.TakeError();
      if (backend.error.empty())
         backend.error = io.GetLastError().Translation().utf8_str().data();
      discard();
      return 4;
   }
   auto &loadedTracks = TrackList::Get(*fresh);
   auto snapshot = SnapshotLoaded(loadedTracks);
   if (!FixTracks(loadedTracks)) {
      backend.error = "the project file has damaged or missing tracks";
      snapshot->Clear();
      discard();
      return 6;
   }
   parsed->Commit();
   ProjectHistory::Get(*fresh).InitialState();
   const bool recovered = io.IsRecovered();
   if (recovered) {
      // PushState also writes a fresh autosave document.
      ProjectHistory::Get(*fresh).PushState(
         XO("Project was recovered"), XO("Recover"));
   }
   // A project that was never saved has no project document in its file and
   // stays an unsaved project; its file is not a user-visible path.
   const std::string newPath = io.IsTemporary() ? std::string{}
      : std::string{ path.utf8_str().data() };

   // Success: retire the previous project.
   if (backend.lastSaved)
      backend.lastSaved->Clear();
   ClearProject(*backend.project);
   backend.project = std::move(fresh);
   backend.lastSaved = std::move(snapshot);
   backend.projectPath = newPath;
   backend.recovered = recovered;
   ++backend.revision;
   return 0;
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
      const int status = LoadProjectFile(*backend, path);
      if (status == 0)
         backend->ReleaseClaim();
      return status;
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

long acetate_backend_pref_int(
   const AcetateBackend *backend, const char *key, long fallback)
{
   if (!backend || !key || !gPrefs)
      return fallback;
   return gPrefs->ReadLong(HostKey(key), fallback);
}

void acetate_backend_set_pref_int(AcetateBackend *backend, const char *key, long value)
{
   if (!backend || !key || !gPrefs)
      return;
   gPrefs->Write(HostKey(key), value);
   gPrefs->Flush();
}

const char *acetate_backend_pref_string(AcetateBackend *backend, const char *key)
{
   if (!backend || !key || !gPrefs)
      return "";
   backend->prefString = gPrefs->Read(HostKey(key), wxString{}).utf8_str().data();
   return backend->prefString.c_str();
}

void acetate_backend_set_pref_string(
   AcetateBackend *backend, const char *key, const char *value)
{
   if (!backend || !key || !value || !gPrefs)
      return;
   gPrefs->Write(HostKey(key), wxString::FromUTF8(value));
   gPrefs->Flush();
}

int acetate_backend_recoverable_count(const AcetateBackend *backend)
{
   return backend ? static_cast<int>(backend->recoverables.size()) : 0;
}

int acetate_backend_recoverable_info(
   const AcetateBackend *backend, int index, double *modifiedSeconds,
   double *bytes)
{
   if (!backend || index < 0 ||
       index >= static_cast<int>(backend->recoverables.size()))
      return 1;
   const auto &entry = backend->recoverables[static_cast<size_t>(index)];
   if (modifiedSeconds)
      *modifiedSeconds = entry.modified;
   if (bytes)
      *bytes = entry.bytes;
   return 0;
}

int acetate_backend_recover(AcetateBackend *backend, int index)
{
   if (!backend)
      return 1;
   backend->error.clear();
   if (index < 0 || index >= static_cast<int>(backend->recoverables.size())) {
      backend->error = "no such recoverable project";
      return 3;
   }
   if (!CanEdit(*backend))
      return 2;
   try {
      const auto entry = backend->recoverables[static_cast<size_t>(index)];
      // Claim the session first: another host may be recovering it too.
      FileLock claim;
      if (!claim.Acquire(entry.dir / "lock")) {
         backend->error = "another Acetate host is using that project";
         return 3;
      }
      std::error_code ec;
      if (!fs::exists(entry.file, ec)) {
         backend->error = "the project file is gone";
         return 3;
      }
      const int status = LoadProjectFile(
         *backend, wxString::FromUTF8(entry.file.string()));
      if (status != 0)
         return status;
      // The old claim, if any, was for the project just closed.
      backend->ReleaseClaim();
      backend->claimDir = entry.dir;
      backend->claimLock = std::move(claim);
      backend->recoverables.erase(
         backend->recoverables.begin() + index);
      return 0;
   }
   catch (const std::exception &e) {
      backend->error = e.what();
   }
   catch (...) {
      backend->error = "unexpected error while recovering";
   }
   return 7;
}

int acetate_backend_discard_recoverable(AcetateBackend *backend, int index)
{
   if (!backend)
      return 1;
   backend->error.clear();
   if (index < 0 || index >= static_cast<int>(backend->recoverables.size())) {
      backend->error = "no such recoverable project";
      return 3;
   }
   const auto entry = backend->recoverables[static_cast<size_t>(index)];
   // Only ever delete a directory this host listed from its own sessions
   // directory.
   if (entry.dir.parent_path() != backend->sessionsDir ||
       entry.dir == backend->sessionDir || entry.dir == backend->claimDir) {
      backend->error = "refusing to delete that directory";
      return 3;
   }
   FileLock claim;
   if (!claim.Acquire(entry.dir / "lock")) {
      backend->error = "another Acetate host is using that project";
      return 3;
   }
   std::error_code ec;
   std::vector<fs::path> contents;
   for (fs::directory_iterator it{ entry.dir, ec }, end; !ec && it != end;
        it.increment(ec))
      if (it->path().filename() != "lock")
         contents.push_back(it->path());
   for (const auto &path : contents)
      fs::remove_all(path, ec);
   claim.Release();
   fs::remove_all(entry.dir, ec);
   if (fs::exists(entry.dir, ec)) {
      backend->error = "could not delete the project";
      return 4;
   }
   backend->recoverables.erase(backend->recoverables.begin() + index);
   return 0;
}

int acetate_backend_was_recovered(const AcetateBackend *backend)
{
   return backend && backend->recovered;
}
