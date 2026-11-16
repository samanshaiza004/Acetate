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

The Zoom tool's press, drag, release, and cancel gestures also pass through Alicorn hit testing. Alicorn now presents the transient dashed drag preview through retained geometry; wx remains the fallback. Audacity's existing `ZoomHandle` still applies zoom policy and updates `ViewInfo`.

The current Alicorn path is an opt-in UI migration prototype hosted by wxWidgets. Alicorn renders selected toolbar controls, including Pause/Play/Stop/Skip to Start/Skip to End, modifier-specific Play modes, the Edit toolbar's Delete/Undo/Redo actions, and File Actions for Import Audio, Save Project, and Export Audio. These buttons dispatch Audacity's existing commands; project selection, playback state, file dialogs, persistence, and export policy remain Audacity-owned. Alicorn also renders the Selection Toolbar's segmented numeric-time values, including formatted labels, focused-digit styling, and the format chevron. Its hosted input owns Left/Right/Home/End digit-focus navigation, routes digit, Backspace, Delete, Up, and Down edit intents, and performs physical hit testing for digit clicks. `NumericTextCtrl` applies those edits through its existing formatter/converter, mirrors focus for Audacity's native accessibility object, and remains authoritative for value changes, format changes, and opening the format menu. wx paint remains the fallback. The Selection Toolbar mode setup opens an Alicorn-rendered radio-option popup while Audacity retains mode changes and preference persistence; the native wx menu remains available as fallback. The Snap checkbox, each action control, and each numeric-time view use Alicorn's `Hosted_Runtime` on their individual wx surfaces. wx owns their windows and event loop, forwards resize, pointer, keyboard, and focus events, and synchronously presents retained commands through wx. The Selection Toolbar numeric fields and four-choice mode popup have passed a native compile, but popup appearance, keyboard behavior, and native accessibility have not been verified in a running application. The TrackPanel waveform uses retained geometry populated from Audacity's visible-range min/max/RMS cache, including bounded rectangle fills for the normal-zoom envelope/background and selected range plus selected-range boundary strokes, as well as retained per-clip boundaries, selected-clip strokes, and passive waveform clip-title text on a separate retained text output. wx now presents rounded clip-affordance fills through the retained geometry bridge; it still owns their thin outlines, buttons, and in-place title editing. Ordinary solid channel backgrounds and selected-track selection partitions also use retained fills when no beat grid is active; sync-lock-only patterns and specialized backgrounds remain on wx. Plain audio-track range drags pass through an Alicorn hit-test surface before Audacity's `SelectHandle` applies snapping, time conversion, selection mutation, history, and cancel rollback; the same adapter now presents transient snap-guide lines from Alicorn retained overlay geometry, with wx drawing them if the retained path fails. Empty-ruler drags that create loop regions also use Alicorn's retained pointer tracking, while Audacity's `NewPlayRegionHandle` remains authoritative for snapping, loop-region state, selection synchronization, and cancellation rollback. Middle-button horizontal pan deltas are derived from Alicorn's retained pointer gesture and applied by Audacity's `Viewport`; wx retains native mouse capture. Wheel events now enter Alicorn as `Scroll_Event`s and are captured by a hidden retained waveform region, while Audacity keeps its existing modifier, scrubbing, zoom, and viewport policy. Alicorn also renders time-ruler ticks/labels from Audacity's generated ruler snapshot, the moving track-panel playback indicator from Audacity's playback-scroller position, and point-selection caret indicators for selected tracks. Ruler and waveform presentation share one lazily created Alicorn runtime owned by `ProjectWindow`; the separate toolbar surfaces use their own hosted runtimes. Retained geometry and clip-title text are still presented through wx `wxGraphicsContext`; this has not replaced the native renderer or proved native GPU pixels.

These are partial slices, not a replacement timeline. wx still owns the main window and event loop, applies project-specific zoom/scroll behavior and pan movement, handles remaining ruler input, portions of track and ruler backgrounds, sync-lock selection patterns, accessibility host objects, envelope controls, clip affordance outlines/buttons and in-place title editing, high-zoom sample editing, and audio services. The shared timeline runtime covers the ruler and waveform; the Snap checkbox and individual action controls use their own `Hosted_Runtime` instances. The bitmap waveform and wx ruler/playhead paths remain fallbacks. Native visual interaction, waveform repaint cost, keyboard editing, and screen-reader behavior have not passed the MVP acceptance gates. Delete/Undo/Redo use Audacity's existing commands but have not been verified as one end-to-end single-track workflow. The wxWidgets workspace remains the functional fallback.

The waveform and ruler surfaces use one `ProjectWindow`-owned Alicorn runtime, while wx continues to host and synchronously dispatch paint/input work. The Snap checkbox and individual action controls use the host-owned `Hosted_Runtime` lifecycle on their native surfaces. The timeline still uses its earlier runtime wrapper, and the SDL_GPU presenter is not integrated into wx. Alicorn routes wheel events through the retained waveform region and supplies middle-button pan deltas; Audacity still applies its project-specific zoom and scroll policy through `ViewInfo` and `Viewport`. Keyboard selection and a validated one-track edit workflow are still outstanding. Open/import/save/export continue through Audacity's existing menus, dialogs, and backend. Do not copy project PCM into a second UI model.

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
