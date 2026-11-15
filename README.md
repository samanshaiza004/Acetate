# Acetate

Acetate is an experimental, editing-first GUI for Audacity 3. It keeps Audacity's proven audio engine, import/export paths, project model, editing operations, undo history, and recovery behavior while replacing the application interface with [Alicorn](https://github.com/samanshaiza004/alicorn), one slice at a time.

This repository is based on Audacity 3.7.9. It is an independent project and is not an official Audacity release.

## MVP target

The first usable workflow is deliberately narrow:

1. Open one mono or stereo audio file.
2. View its waveform against a time ruler, then zoom and scroll.
3. Select a time range and play from the cursor or selection.
4. Delete a selected range and undo or redo the edit.
5. Save and reopen an Audacity project, then export WAV.

Multitrack editing, recording, effects and plugins, clip movement, and advanced export are later milestones. Existing projects with content the MVP cannot represent must remain openable through Audacity's complete workspace; Acetate must never silently discard unsupported project data.

## Current implementation status

The current Alicorn path is an opt-in native-control prototype hosted by wxWidgets. It renders selected toolbar controls through Alicorn, uses Alicorn-retained geometry for normal-zoom waveform summary strokes from Audacity's visible-range min/max/RMS cache, and routes plain audio-track range drags through an interactive Alicorn surface. The wx `SelectHandle` remains the adapter for track eligibility, snapping, time conversion, selection mutation, history, and cancel rollback; Audacity's project selection remains authoritative. Sample-accurate behavior across zoom and scroll, keyboard range editing, and native interaction have not yet passed the MVP acceptance gates. wx still owns the main window, event loop, accessibility host objects, track-panel background and selection fill, envelope controls, clip decorations, high-zoom sample editing, and audio services. The bitmap waveform renderer remains the fallback. The bridge's repaint cost has not yet been benchmarked against the bitmap cache. The complete editing workflow above is not implemented yet, and the wxWidgets workspace remains the functional fallback.

The first broad UI milestone is a native host surface with retained Alicorn state. The waveform work is still a partial rendering and input slice, not a replacement of the track editor; transport, ruler, zoom/scroll, delete, undo/redo, and project open/save/export remain future work. Do not copy project PCM into a second UI model.

## Architecture boundary

- **Audacity/backend:** project and track data, audio transport, edits, undo/redo, file I/O, export, and recovery.
- **Caliber:** bounded, versioned commands and revision-tagged state exchange as backend operations migrate to Odin.
- **Alicorn:** layout, waveform and control presentation, pointer/keyboard interaction, focus, semantics, and transient viewport state.
- **wxWidgets during migration:** native host and platform services until Alicorn can integrate with Audacity's existing UI thread and event loop without nesting loops or losing native behavior.

The UI must not become authoritative for project data, samples, selection, or undo history. Keep real-time audio callbacks out of the general application command transport.

## Build

Start with Audacity's [build instructions](BUILDING.md). The experimental Alicorn controls are disabled by default. To enable them, provide a clean Alicorn checkout at the revision in [`AlicornSnapUIPin.cmake`](cmake-proxies/cmake-modules/AlicornSnapUIPin.cmake), install the Odin compiler, and configure with:

```sh
cmake -S . -B build \
  -Daudacity_use_alicorn_snap_ui=ON \
  -DAUDACITY_ALICORN_SOURCE=/path/to/alicorn
cmake --build build --target Audacity
```

The Caliber backend option is separate. Use `-Daudacity_use_odin_caliber_backend=ON` only when validating that backend integration, with its required source and compiler settings. The Alicorn and Caliber paths are still experimental; a successful build does not establish MVP interaction, audio, accessibility, or project-safety acceptance.

## Project documents

- [MVP GUI proposal and acceptance gates](docs/research/audacity-mvp-gui-proposal.md)
- [Alicorn native-control bridge](src/alicorn/README.md)
- [Caliber backend boundary](CALIBER.md)
- [Audacity upstream README](AUDACITY-UPSTREAM-README.md)

## Lineage and license

Acetate retains Audacity source and its licensing. The upstream project describes Audacity as GPLv3, while most code files are GPL-2.0-or-later; individual files and bundled libraries may have separate notices. See [`LICENSE.txt`](LICENSE.txt), file headers, and relevant third-party licenses. Preserve upstream attribution when modifying or moving files.
