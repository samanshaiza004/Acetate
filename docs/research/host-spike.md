# Alicorn-owned host window spike

Status: experimental, opt-in, macOS verified only (2026-11-17). This is a feasibility spike for the "host swap" step in `alicorn-frontend-roadmap.md`. It does not replace or change the wx application.

## What it proves

An executable whose window, event loop, layout and paint belong to Alicorn (`native/sdl_gpu` application host, `alicorn:ui`) can drive Audacity's own C++ project and track code without any wxWidgets window:

1. `host-spike/backend` is a shared library with a small C ABI (`AcetateBackend.h`, ABI version 1). It creates an `AudacityProject`, opens its temporary project database (`ProjectFileIO::OpenProject`), decodes a mono/stereo file with libsndfile and appends it to a `WaveTrack` made by `WaveTrackFactory`, so samples live in Audacity's SQLite sample blocks.
2. Display data comes from `WaveChannelUtilities::GetMinMax` for N columns of the visible range. Only 2 floats per column cross the ABI; no PCM buffer is copied into UI state.
3. `host-spike/app/main.odin` is the Alicorn host: one retained GPU geometry surface per channel, rebuilt only when the surface size or backend revision changes (one filled rectangle per column, capped at 1024 columns to stay under the 8192-vertex surface limit).

Verified on macOS arm64: the backend smoke test imported a 3 s stereo WAV (reported 2 channels, 44100 Hz, end 3.000 s, revision 1) and returned envelope-shaped min/max values; the host `--smoke` run passed (SDL cocoa, Metal); and the live window showed both channels with correct relative amplitude (about 0.61 and 0.37 for the generated test tone).

## Build

```sh
cmake -DCMAKE_BUILD_TYPE=Release -Daudacity_host_spike=ON <build-dir>
ninja -C <build-dir> acetate-host-backend
host-spike/build_host.sh [alicorn-source] [build-dir]
<build-dir>/host-spike/acetate-host path/to/file.wav
```

Needs Odin and SDL3 (as for Alicorn's examples) and the pinned Alicorn source (`AUDACITY_ALICORN_SOURCE`, default `~/dev/alicorn-pin`). The pin was not changed. `audacity_host_spike` is off by default, so nothing changes for normal builds. On macOS the library is placed in `host-spike/lib` with a `Frameworks` symlink to the app bundle's libraries, because Audacity's libraries record `@loader_path/../Frameworks` as their install path.

## Backend initialization the headless path needed

These are the things the wx application normally does and a host must do itself:

- `audacity::sqlite::Initialize()`: the bundled SQLite is built without auto-initialisation (the first attempt crashed in `openDatabase`).
- A writable temp directory registered with `TempDirectory::SetDefaultTempDir` and `FileNames::UpdateDefaultPath(Temp, ...)`. A harmless wx log line ("Failed to get file system statistics") still appears from the free-space check; not yet traced.
- Preferences: the spike uses the unit tests' in-memory `MockedPrefs`, so it ignores the user's real settings. A real host needs the persisted preferences store.
- PortAudio is initialised (as the test fixtures do) but no stream is opened.

## Not done (and not claimed)

- Playback/stop through Audacity's audio engine. `AudioIO` setup headless and the transport/Caliber boundary are the next unknown.
- Time ruler, selection, delete, undo/redo, `.aup3` save/reopen, WAV export.
- Importers: the spike decodes with libsndfile directly; Audacity's `Importer` plugins live in modules (`mod-pcm`) that are not linked here, so format coverage and tags differ from the app.
- Input events, accessibility, IME, native dialogs and file picker, menus, Windows/Linux builds.
- The backend runs on the host's main thread; no audio/worker thread touches UI.

## Next slices

1. Transport: construct `AudioIO` headless, play/stop, and report play position (revision-tagged) to the host.
2. Selection and delete/undo through `ProjectHistory`, with the host sending bounded commands.
3. Real preferences and `.aup3` save/reopen, then WAV export via `Exporter` (module linking decision needed).
