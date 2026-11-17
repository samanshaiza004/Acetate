/*  SPDX-License-Identifier: GPL-2.0-or-later */
#include "AcetateBackend.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <sndfile.h>

#include "MockedPrefs.h"
#include "Prefs.h"
#include "Project.h"
#include "ProjectFileIO.h"
#include "AudioIO.h"
#include "FileNames.h"
#include "SampleFormat.h"
#include "StretchingSequence.h"
#include "TempDirectory.h"
#include "sqlite/SQLiteUtils.h"
#include "Track.h"
#include "WaveChannelUtilities.h"
#include "WaveTrack.h"

// Matches the unit-test fixtures: PortAudio is only initialised so lib-audio-io
// static state is valid; no stream is opened by this slice.
#include <portaudio.h>

struct AcetateBackend {
   // The headless spike keeps settings in memory instead of touching the
   // user's Audacity preferences.
   MockedPrefs prefs;
   bool paInitialized{ false };
   std::shared_ptr<AudacityProject> project;
   unsigned long long revision{ 0 };
   std::string error;
   std::filesystem::path tempDir;

   AcetateBackend()
   {
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
   }
   ~AcetateBackend()
   {
      if (auto *audioIO = AudioIO::Get())
         if (audioIO->IsBusy())
            audioIO->StopStream();
      if (project)
         ProjectFileIO::Get(*project).CloseProject();
      project.reset();
      AudioIO::Deinit();
      if (paInitialized)
         Pa_Terminate();
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
