# Alicorn-owned host window spike

Status: experimental, opt-in, macOS verified only (2026-11-17). This is a feasibility spike for the "host swap" step in `alicorn-frontend-roadmap.md`. It does not replace or change the wx application.

## What it proves

An executable whose window, event loop, layout and paint belong to Alicorn (`native/sdl_gpu` application host, `alicorn:ui`) can drive Audacity's own C++ project and track code without any wxWidgets window:

1. `host-spike/backend` is a shared library with a small C ABI (`AcetateBackend.h`, currently ABI version 5). It creates an `AudacityProject`, opens its temporary project database (`ProjectFileIO::OpenProject`), decodes a mono/stereo file with libsndfile and appends it to a `WaveTrack` made by `WaveTrackFactory`, so samples live in Audacity's SQLite sample blocks.
2. Display data comes from `WaveChannelUtilities::GetMinMax` for N columns of the visible range. Only 2 floats per column cross the ABI; no PCM buffer is copied into UI state.
3. `host-spike/app/main.odin` is the Alicorn host: one retained GPU geometry surface per channel, rebuilt only when the surface size or backend revision changes (one filled rectangle per column, capped at 1024 columns to stay under the 8192-vertex surface limit).

4. Transport (ABI 3): `acetate_backend_play/stop/update/is_playing/play_position` start Audacity's own `AudioIO` stream over `StretchingSequence`s of the project's wave tracks (the same sequences `MakeTransportTracks` builds in the app) and report the stream position. The host polls from a 33 ms scheduled wake on its main thread, draws an orange playhead into the same retained surfaces, and toggles with Space (`Application_Key.Toggle`) or the Play/Stop button. Audacity's audio thread only fills the device; it never calls into the host. A stream that reaches its end is finalised by the host's `acetate_backend_update` tick, as the app does from its own timer.

5. Selection and edits (ABI 4): the time selection is Audacity's `ViewInfo::selectedRegion`. `acetate_backend_delete_selection` mirrors `OnDelete` in `src/menus/EditMenus.cpp` (`Clear` on each wave track, collapse the selection, `ProjectHistory::PushState`), and undo/redo run `UndoManager::Undo/Redo` with `ProjectHistory::PopState`, as the app's handlers do. Import pushes an "Imported audio" state and `InitialState()` runs after project creation, so undo past the import empties the project, as in the app. Edits are refused while the audio engine is busy. The host selects by dragging on a waveform surface (a `.Pointer` GPU surface; `on_pointer` maps x to time and calls `set_selection`; Escape clears), shades the selection over every channel, and exposes Edit > Undo / Redo / Delete as native menu items with Cmd+Z, Cmd+Shift+Z and Backspace accelerators, enabled from backend state.
6. Headless `BasicUI::Services`: Audacity code reports errors and requests progress dialogs through `BasicUI`. Without an implementation, closing a project with a pending database checkpoint dereferenced a null progress dialog (hit once edits created more commits). The shim installs a small implementation that logs and records errors, returns no-op progress dialogs, and queues `CallAfter` work for `acetate_backend_update`.

7. Project files and export (ABI 5): `acetate_backend_save` calls `ProjectFileIO::SaveProject` (a Save As onto an existing different file uses `BackupProject`, as `ProjectFileManager::DoSave` does; saving onto the project's own file does not, because renaming the open file would lose it) and then `StateSaved` plus a fresh "last saved" snapshot of the tracks. `acetate_backend_open` creates a fresh project, opens its temporary connection, runs `LoadProject`, applies the loader fix-ups, calls `InitialState` and only then replaces the old project; any failure keeps the current one. `acetate_backend_export_wav` runs Audacity's real `ExportPCM.cpp` plug-in (compiled into the shim, so no `mod-pcm` module is loaded) through `ExportPluginRegistry` with default parameters: 16-bit PCM with Audacity's usual dither, all wave tracks mixed over the whole project, stereo if any track is stereo. The host adds File > Open, Save, Save As and Export WAV with native, asynchronous file dialogs (`services.dialogs`, results via `on_dialog`), Cmd+O / S / Shift+S / Shift+E accelerators, and shows the project path with `*` when `UnsavedChanges()` is set. A `.aup3` argument opens a project; any other argument is imported.

Verified on macOS arm64: the backend smoke test imported a 3 s stereo WAV (reported 2 channels, 44100 Hz, end 3.000 s, revision 1) and returned envelope-shaped min/max values; the host `--smoke` run passed (SDL cocoa, Metal); and the live window showed both channels with correct relative amplitude (about 0.61 and 0.37 for the generated test tone). For transport, a C test played 0.5 s to 2.0 s (position advanced 0.545 to 2.000, then the stream ended and `is_playing` went to 0), replayed, and stopped mid-stream; in the live window Space started playback (button "Stop", status "Playing", playhead at about 1.1 s), the stream ran to the end ("Finished", playhead cleared), and a second Space pair started and stopped it ("Stopped"). The tone was audible through the default output device; audio quality, latency and device-change behavior were not assessed. The Play/Stop button was not clicked with the mouse, only the Space path was driven. For edits, a C test covered delete with no selection (refused), delete of 1.0 to 2.0 s (project end 3.000 to 2.000), undo (3.000 and selection restored), redo, double undo/redo, and refusal while playing, and teardown no longer asserts. In the live window a synthesized mouse drag selected 0.996 to 2.004 s (readout and shading on both channels), Backspace deleted it (3.000 s to 1.992 s, the gap closed in both waveforms), Cmd+Z restored 3.000 s and Cmd+Shift+Z re-applied the delete; the Edit menu items Undo, Redo and Delete also worked from the menu bar. For save and export: a C round trip imported a stereo tone, deleted 1.0 to 2.0 s, saved `.aup3`, exported WAV, reopened in a fresh backend and re-exported. Both exports were 16-bit stereo with the expected length and left/right peaks (0.61 and 0.37); samples are not bit-identical between exports (differences of a few LSB, mean about 2, consistent with 16-bit dither; not separately confirmed). Open replaces a loaded project with its history cleared; opening a bad path (missing, or not `.aup3`) leaves the current project intact; Save As onto an existing file replaces it with no leftover backup, and both files reopened with the right length. In the live window, Save As through the native sheet saved the edited project and cleared the `*`; relaunching the host with that `.aup3` showed the same 2.384 s, both channels and the deletion gap; an edit then Cmd+S saved in place and relaunching showed 2.042 s; Export WAV through the sheet wrote a stereo 2.384 s file with the right peaks. Not exercised: completing File > Open through the native panel (the panel opened and selected the file, but synthesized typing into the modal Go to Folder box could not be dismissed), typing file names into the panels, saving a project while its tracks changed sample rate, recovery of unsaved projects, and any non-Latin or very long paths. Earlier items not exercised: Escape clearing, dragging to the right of the surface or outside the window, edits from a second channel surface, selecting while playing, and whether Delete/Backspace conflicts with text fields (the host has none).

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
- PortAudio is initialised, then `AudioIO::Init()` constructs the engine (and its audio thread); the destructor stops any stream before `AudioIO::Deinit()`. Device preferences come from the in-memory mock, so the system default output is used.

## Not done (and not claimed)

- Transport policy beyond play-all/stop: pause, seek, loop, play-selection, scrub, speed, the Caliber transport boundary, and play-position events (the host polls). Recording, meters and device selection are not wired.
- Export format and options UI, tags, selection-only and multi-format export, and import of formats other than what libsndfile reads. Save, open and export block the main thread (fine for short files; a long export would freeze the window and needs a worker design that cannot race with edits).
- Recovery of crashed sessions: the temporary project is deleted at close; autosave is not wired.
- A time ruler, selection edges that can be dragged after the fact, zoom/scroll (the view always spans the whole project), per-track selection and the other editing commands (cut, copy, paste, split, silence), and `.aup3` save/reopen, WAV export.
- Selection and undo are not revision-tagged commands through Caliber; the host calls the shim directly. The window shows duration and selection as text only.
- Importers: the spike decodes with libsndfile directly; Audacity's `Importer` plugins live in modules (`mod-pcm`) that are not linked here, so format coverage and tags differ from the app.
- Input events, accessibility, IME, native dialogs and file picker, menus, Windows/Linux builds.
- The backend runs on the host's main thread; no audio/worker thread touches UI.

## Backend gaps found

The wx-coupled `ProjectFileManager` holds logic a host must otherwise copy: `FixTracks` (re-zipping stereo channels after load) and the "last saved" track snapshot (keeps the saved file's blocks alive while editing). The shim carries copies in `AcetateBackend.cpp`; without `FixTracks`, a reopened stereo project silently lost its right channel. These, `DoSave`'s backup sequence, and the `BasicUI` services Audacity code assumes, should live in a UI-free library before a second host depends on them. Nothing is needed from Alicorn so far.

## Next slices

1. Real preferences and autosave/recovery, so the host stops using the in-memory mock settings and can recover a crashed session.
2. A time ruler with zoom and scroll, so waveform columns come from a visible range instead of the whole project.
3. Move the copied project logic into a library and route selection/edits/transport through the revision-tagged Caliber commands.
