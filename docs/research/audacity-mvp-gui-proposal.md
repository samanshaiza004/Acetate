# Audacity 3 Alicorn GUI MVP

**Status:** Accepted product scope; Acetate implementation is underway. This document records the target workflow and acceptance gates, not completion status.

**Target:** Audacity 3.7.9, the newest Audacity 3 release listed by the official release page at the time of research. Audacity 4 is a separate product direction; this proposal targets the Audacity 3 behavior and project format. See the [Audacity releases](https://github.com/audacity/audacity/releases) and [3.7.9 notes](https://github.com/audacity/audacity/releases/tag/Audacity-3.7.9).

## Recommendation

Build an **editing-first, single-track MVP** as an experimental Alicorn workspace inside the Audacity 3 application. Make one end-to-end audio-editing loop reliable before migrating the full multi-track workbench:

> Open one supported audio file → view its waveform on a timeline → select a time range → play/pause/stop → delete the selected range → undo/redo → save and reopen the project → export a WAV.

The MVP supports one mono or stereo audio track. Basic edits may split that track into clip segments and gaps, but the user cannot move, resize, or mix clips. There is no recording, multitrack mixing, effects, or plugin management in this first version.

This is deliberately smaller than Audacity's defining multitrack workflow. That tradeoff is acceptable for a first Alicorn editor because it isolates the hardest new GUI surface—waveform rendering, time selection, transport feedback, and safe project persistence—without first rebuilding every TrackPanel mode. Keep the complete Audacity 3 workspace available as a fallback until the MVP has passed its acceptance gates.

## Product scope

### Included

- A single project window and one audio track, mono or stereo.
- Open/import one audio file through Audacity's existing importers. The MVP does not implement codecs; the importer remains the format authority.
- A waveform overview, seconds ruler, horizontal scroll and zoom, playhead, and visible time selection.
- Pointer drag selection with sample-accurate endpoints, plus keyboard-accessible selection controls and readable start/end values.
- Play, pause, and stop from the cursor or selection through Audacity's existing audio engine, using the current default output device.
- Delete the selected range using Audacity's existing edit semantics; undo and redo through the project history.
- Save/reopen as `.aup3` and export the complete one-track project as WAV.
- Basic keyboard operation, focus visibility, and semantic names/values for transport, selection, zoom, and file actions.
- Existing native file dialogs and platform services may remain hosted by wxWidgets during this migration slice.

### Deferred until after the MVP

- Multiple tracks, mixing, mute/solo, gain/pan, sync lock, and track grouping.
- Recording, input-device selection, monitoring, punch-and-roll, and latency controls.
- Direct clip movement/resizing, fades, envelope automation, clipboard cut/copy/paste, and cross-track operations.
- Effects, generators, analyzers, plugin hosting, presets, and realtime effects.
- Label, MIDI/note, time, and spectral tracks or views.
- Macros, scripting, batch workflows, advanced export options, cloud features, and the full preferences surface.

Accessibility is not a deferred feature. The first workspace needs a usable keyboard path and an accessible way to inspect and set the time range. Richer screen-reader interaction can expand alongside the later track and clip features.

## Project compatibility and data safety

Audacity project files are not interchangeable with exported audio. An `.aup3` stores project tracks, clips, labels, envelopes, gain/pan, and other editing data; export produces a playable audio file. The project also carries editing and recovery responsibilities, so the MVP must reuse Audacity's project I/O and recovery path rather than inventing a second project format. See [Managing Audacity Projects](https://manual.audacityteam.org/man/audacity_projects.html).

The MVP may open an `.aup3` only when it can represent and preserve every project object in that file. If a project contains unsupported tracks, effects, or other data, offer to open it in the complete legacy workspace. Never silently flatten, omit, or rewrite unsupported content. Saving from the MVP must preserve the same project data that was opened.

The full wxWidgets workspace remains available during rollout. The Alicorn workspace becomes eligible for default use only after import, edit, save/reopen, export, accessibility, and recovery behavior pass on supported platforms.

## Responsibility boundary

| Area | Owns |
|---|---|
| Audacity application/backend (Odin/Caliber as the rewrite advances) | Project and track data, authoritative time selection, audio transport, edits, undo/redo, file I/O, export, and error/recovery policy |
| Caliber boundary | Versioned, bounded application commands and revision-tagged state exchange; schema and command meaning remain Audacity-owned |
| Alicorn | Timeline layout, waveform presentation, input gestures, focus, semantics, and transient viewport state such as zoom and scroll |
| wxWidgets host during migration | Native window/event-loop integration, OS dialogs, clipboard/device services that have not moved, and platform bridging |

The GUI must not keep a second authoritative copy of the project, sample data, selection, or undo history. Pointer selection sends a value to the application layer; subsequent UI state is derived from the accepted application snapshot.

Use stable project/track identities and monotonically increasing revisions. A waveform response or edit based on an old revision must be discarded or rejected. The waveform request should describe only the visible time window and requested display resolution. Return bounded per-channel min/max summaries (and other display summaries only if the UI needs them), not entire PCM buffers on every frame. The real-time audio path must stay outside Caliber's general command queue.

The current migration should preserve an implementation seam: Alicorn consumes the Audacity application contract, while Odin/Caliber progressively takes ownership of the operations behind it. Do not expose C++ object layouts or private runtime storage across that boundary. If an MVP operation is not yet available in the Odin backend, identify it explicitly as a backend gate; do not implement a second copy of its policy in the UI.

## Implementation sequence

### 0. Freeze the support contract

Document the one-track project eligibility rules and the exact edit semantics. Define the state and commands the workspace needs: open/import, read project summary, request a visible waveform window, set selection, play/pause/stop, delete, undo/redo, save, and export WAV. Keep error, cancellation, progress, and stale-revision behavior explicit.

### 1. Establish the host boundary

The host-owned `Hosted_Runtime` API is implemented in Alicorn (#196 is complete). Acetate's Snap checkbox, selected action controls, and Selection Toolbar numeric-time presentation use it on wx-hosted surfaces; wx still owns the event loop and presents them synchronously through `wxGraphicsContext`. For numeric time, Alicorn paints the formatted digits, labels, focus highlight, and format chevron, and its hosted input handles Left/Right/Home/End digit-focus navigation and physical hit testing for digit clicks. `NumericTextCtrl` remains authoritative for digit entry, increment/decrement, format changes, focus/tab traversal, the menu, and native accessibility; wx painting remains the fallback. This does not migrate native text input or establish visual parity. The Odin wrapper and full Audacity target were compile-validated on macOS arm64 against Acetate's exact Alicorn pin `e56cef4959dba7eca7134c5b65da7c46d45f56c4` with Odin `dev-2026-11:9a6991685`. The timeline uses one direct Runtime with disjoint ruler, waveform-geometry, and waveform-label outputs, and each synchronous wx paint callback acknowledges that output's exact revision. Alicorn #205's multi-output API is implemented and dogfooded, but native independent-update and idle behavior still need confirmation. This is a per-surface adoption, not an app-wide hosted session or native SDL_GPU presentation. Alicorn #162's renderer-neutral presenter is complete; Acetate has not adopted it. Alicorn #204's swapchain-safe failure recovery and Acetate's native wx/SDL validation remain gates before using the SDL_GPU presenter. Continue validating resize, focus, pointer capture, keyboard/IME, accessibility, scheduling, and teardown without nesting an event loop.

### 2. Deliver the read-only waveform workspace

The visual timeline slice now combines normal-zoom waveform summaries from Audacity's bounded cache, selected-range boundaries from Audacity's authoritative selection, ruler marks/labels, selected/loop-region fills and loop-boundary markers, plus the track-panel playback indicator in one retained runtime per project window. Normal-zoom envelope and selection fills travel as bounded retained rectangles alongside waveform segments. At high zoom, connected sample lines and clipping marks plus the waveform background, envelope, and selection fills use bounded retained geometry. Draggable sample glyphs now travel through Alicorn's compact six-vertex point-marker payload instead of its 48-vertex filled circles, so dense sample editing can remain within the shared 8,192-vertex cap at ordinary viewport widths. The combined high-zoom batch falls back atomically to wx when it exceeds the surface budget or cannot be submitted. The C++ paint callback still presents retained geometry and passive waveform clip-title text through wx; this is not yet native Alicorn GPU presentation. Rounded clip-affordance fills now use a separate retained geometry owner encoded as bounded rectangles and point markers; oversized or rejected batches fall back to wx. wx still paints the thin outlines, buttons, and in-place title editing. Audacity continues to own sample hit-testing and edits. Ruler fill and marker changes update retained geometry without rebuilding the ruler description. Alicorn draws the zero line through retained geometry on normal- and high-zoom separated-background paths; wx still draws sync-lock patterns and remains the fallback for over-budget geometry and track-panel play-region and blank-track backgrounds. Alicorn routes ruler pointer gestures for creating, resizing, and moving loop regions, and tracks middle-button horizontal panning. Audacity retains loop-region rules and `Viewport` applies the horizontal scroll. Native pixel parity and repaint-cost validation remain open. Continue using cached summaries rather than copying or scanning the whole audio buffer during pointer movement. Preserve the existing import and playback engines.

### 3. Complete the edit loop

Connect sample-accurate selection to Audacity's authoritative selection model. Add delete, undo, redo, dirty-state reporting, and playback of the selected range. Keep edit policy and history in the backend.

### 4. Add persistence and export, then validate recovery

Use the existing `.aup3` read/write and recovery machinery for supported projects, and the existing WAV export path. Test save/reopen fidelity, interrupted saves, recovery after an abnormal close, and failure reporting such as a full disk. Do not promote the MVP while those paths are incomplete.

### 5. Roll out behind a feature flag

Keep the complete wxWidgets workspace available for unsupported projects and as a rollback path. Enable the Alicorn workspace by default only after native validation on macOS, Windows, and Linux, including real file I/O and audio-device sessions.

## Existing Alicorn dependencies

These are already tracked by Alicorn and should be advanced instead of duplicated:

- [#196 — Integrate Alicorn with an existing desktop event loop](https://github.com/samanshaiza004/alicorn/issues/196): completed; Acetate is adopting the host-owned Runtime incrementally.
- [#162 — Host-owned renderer-neutral presentation boundary](https://github.com/samanshaiza004/alicorn/issues/162): completed; Alicorn's `Sdl_GPU_Presenter` is implemented. Acetate remains on wx presentation; adoption is gated on #204 and native wx/SDL validation.
- [#164 — Custom GPU canvas experiment](https://github.com/samanshaiza004/alicorn/issues/164): completed with a no-new-API conclusion. Alicorn's retained geometry surface expressed the waveform; renderer-side caching reduced CPU encoding p95/p99 from 58.8/75.8 µs to 22.9/31.9 µs, uploads from 227,232 B to 5,184 B per frame, and geometry passes from 4 to 2. [Research report](https://github.com/samanshaiza004/alicorn/blob/6410ba38cc51e8dfd929a36524838db185d679ab/docs/research/issue-164-gpu-canvas.md).
- [#200 — Bounded filled-rectangle batches for retained geometry](https://github.com/samanshaiza004/alicorn/issues/200): Alicorn's retained rectangle API and Acetate's bounded fill payload are implemented; native readback and p95 rendering-cost acceptance remain.
- [#204 — Swapchain-safe presenter failure recovery](https://github.com/samanshaiza004/alicorn/issues/204): open; must be resolved before Acetate can safely use the SDL_GPU presenter with an acquired window swapchain target.
- [#205 — Coordinate frame acknowledgements across multiple native surfaces](https://github.com/samanshaiza004/alicorn/issues/205): the multi-output API is integrated for Acetate's ruler and waveform; native independent-update and idle validation remains.

The Acetate bridge carries surface, icon, text, and retained geometry payloads through wx; the ruler and waveform use separate outputs from one Runtime and are synchronously mapped to their native drawing contexts. The Snap checkbox and selected action controls use `Hosted_Runtime`. Do not create a parallel renderer inside the editor. Alicorn #162's renderer-neutral presenter is implemented; Acetate-specific SDL_GPU use remains gated by #204 and native validation. Alicorn #164 is complete: its cached retained-geometry experiment found a renderer optimization opportunity, not a missing application-facing canvas API. #200's rectangle API is implemented; its native readback/performance acceptance remains open.

## Acceptance gates

1. **Import and view:** open a supported mono and stereo file; preserve sample rate/channel data; display the correct duration and waveform at multiple zoom levels.
2. **Selection:** drag in both directions, change zoom/scroll, and verify endpoints map to the expected source sample indices. Keyboard users can inspect and set the same range.
3. **Transport:** play from the cursor and selected range, stop, and verify the playhead follows the backend state without continuous polling while idle.
4. **Editing:** delete a range, verify the exact resulting audio/clip structure, then undo and redo and compare the source data.
5. **Persistence:** save and reopen `.aup3`, verify samples and project state, and exercise recovery/failure paths. Unsupported project content must open in the full workspace without mutation.
6. **Export:** export WAV and compare channel count, sample rate, duration, and decoded samples with the expected result.
7. **Performance:** for a long stereo file, waveform work scales with visible display resolution rather than total project duration. A selection drag must not trigger a full PCM copy, full-project decode, or Alicorn description rebuild for every motion event. Record p50/p95 input-to-visible and frame costs on each native backend before fixing release budgets.
8. **Platform behavior:** native builds and manual interaction pass on macOS, Windows, and Linux; include keyboard focus, screen-reader discoverability, real audio playback, file dialogs, and window close/recovery.

Use `alicorn:testui` for deterministic selection, keyboard, focus, and viewport assertions. It cannot prove actual GPU rendering, native audio behavior, file durability, or OS accessibility, so pair it with native integration checks.

## Expansion path toward Audacity 3 parity

1. **Composition:** multiple audio tracks, simple alignment, track selection, mute/solo, gain/pan, and basic mixing.
2. **Clip editing:** move/resize, split/join, trim, fades, clipboard operations, and undo semantics across tracks.
3. **Recording:** input/device selection, monitoring, latency, punch-and-roll, and recording recovery.
4. **Processing:** destructive and realtime effects, generators, analyzers, plugin formats, and presets.
5. **Additional media and views:** labels, MIDI/note and time tracks, spectral selection, spectrograms, envelopes, and specialized track controls.
6. **Power workflows:** macros/scripting, advanced export, preferences, batch operations, multi-window behavior, and remaining accessibility/platform parity.

Each addition should be an end-to-end vertical slice: backend command and persistence semantics, Alicorn interaction and semantics, native platform validation, and a regression fixture. Keep the old workspace until each supported project feature has a no-data-loss route through the new UI.

## Estimate and current progress

The current Alicorn work covers selected toolbar controls and a shared project-window Runtime for waveform, ruler, selected-range/loop-region fills, waveform selection edges, channel backgrounds and selected-track selection partitions, per-clip boundaries and selected-clip strokes, passive clip-title text on a separate retained output, high-zoom connected sample lines, compact point markers and background fills, playhead presentation, and pointer tracking for waveform selection, loop-region creation/resizing/movement, middle-button horizontal panning, and wheel-event targeting. The ruler, waveform geometry, and waveform labels are bound to disjoint presentation outputs, each with exact-revision acknowledgement after its synchronous wx paint callback. Normal-zoom fills use first-class retained rectangles, while high-zoom line, marker, and rectangle geometry stays within Alicorn's bounded surface payload or falls back atomically to wx. The wx paint callbacks still present timeline geometry and clip-title text; Acetate does not use `Sdl_GPU_Presenter`. The Snap checkbox and selected action controls use `Hosted_Runtime`, while the timeline retains its direct Runtime lifecycle. Selection, sample editing, loop-region policy, and viewport zoom/scroll state and mutation remain Audacity-owned. Native wheel input is normalized into Alicorn `Scroll_Event`s and captured by a hidden waveform `Scroll_Region`; the existing `MouseWheelHandler` still applies modifier, scrub, selection-anchor, and `ViewInfo`/`Viewport` behavior. Ruler range-only repaint updates retained geometry without rebuilding its description. `SelectionBar::ApplyTimeControlValue` separates selection-mode calculations from `NumericTextCtrl`. The two Selection Toolbar numeric-time fields opt into Alicorn painting for formatted segments, focus styling, labels, and the format chevron; Alicorn's hosted input now owns Left/Right/Home/End digit-focus navigation and physical digit hit testing. `NumericTextCtrl` mirrors focus for the native accessibility object and remains the wx-hosted owner of digit entry, increment/decrement, format changes, tab traversal, and the context menu; other numeric controls still use wx painting. wx painting remains the fallback. This is a narrow input migration: wx still translates native events, and native text/IME handling and accessibility remain native. Full visual parity has not been validated. A manual interaction pass using imported raw PCM exercised timeline selection, selected-range playback, delete, undo, and redo; this is preliminary evidence, not completion of the MVP workflow. On 2026-11-15, the opt-in macOS arm64 `Audacity` target built against the exact pinned Alicorn revision `e56cef4959dba7eca7134c5b65da7c46d45f56c4` with Odin `dev-2026-11:9a6991685`. A disposable copy of that app bundle contains `mod-pcm.so`, and its isolated Portable Settings profile records `mod-pcm=1` and the bundled module path; this corrects the earlier observation that module discovery was absent. Both direct launch probes (`--version` and no arguments) used `/private/tmp/acetate-module-probe-default.app/Contents/MacOS/Audacity` and terminated with exit code 134 (SIGABRT). The macOS reports at 22:09 and 22:22 show `NSApplication` initialization aborting in `___RegisterApplication_block_invoke` through `wxApp::DoInitGui`; neither reached Audacity's app-level startup. This narrows the immediate failure to native AppKit registration, but the reports do not establish why registration aborted. The desktop bridge showed an Automatic Crash Recovery dialog; it was left untouched, and the offered project/session was not identified. WAV import has not yet been verified through the UI. Supported-file import therefore remains an open acceptance gate, as do save/reopen, export, and native accessibility. By visible UI surface, this remains roughly **5–10% of the GUI migration**. The core edit loop is partially exercised but not accepted because supported-file import and persistence/export gates remain open. These are planning estimates, not measured code-completion metrics.

Assuming the selected project, audio, import, and export operations are available behind the application boundary, estimate **8–14 engineer-weeks** for the hosted waveform editor, core edit loop, persistence, recovery, accessibility, and native validation (about 2–4 calendar months for one experienced engineer). This excludes completion of the broader Odin/Caliber backend rewrite; the total schedule depends on which of those operations still need to be ported. Re-estimate after the window-host spike and the first import→waveform→playback vertical slice.

Full parity is not responsibly dateable from this MVP estimate. It is multiple feature and platform-validation cycles beyond the first release.

## Sources

- [Audacity 3.7.9 release notes](https://github.com/audacity/audacity/releases/tag/Audacity-3.7.9) and [official releases list](https://github.com/audacity/audacity/releases).
- Audacity manual: [Importing Audio](https://manual.audacityteam.org/man/importing_audio.html), [Selecting Audio](https://manual.audacityteam.org/man/audacity_selection.html), [Playback](https://manual.audacityteam.org/man/playback.html), and [Managing Audacity Projects](https://manual.audacityteam.org/man/audacity_projects.html).
- Audacity 3.7.9 source: [`TrackPanel.cpp`](https://github.com/audacity/audacity/blob/Audacity-3.7.9/src/TrackPanel.cpp), [`WaveformView.cpp`](https://github.com/audacity/audacity/blob/Audacity-3.7.9/src/tracks/playabletrack/wavetrack/ui/WaveformView.cpp), [`WaveDataCache.cpp`](https://github.com/audacity/audacity/blob/Audacity-3.7.9/libraries/lib-wave-track-paint/waveform/WaveDataCache.cpp), [`WaveClip.h`](https://github.com/audacity/audacity/blob/Audacity-3.7.9/libraries/lib-wave-track/WaveClip.h), [`SelectedRegion.h`](https://github.com/audacity/audacity/blob/Audacity-3.7.9/libraries/lib-time-frequency-selection/SelectedRegion.h), [`ProjectAudioManager.cpp`](https://github.com/audacity/audacity/blob/Audacity-3.7.9/src/ProjectAudioManager.cpp), and [`EditMenus.cpp`](https://github.com/audacity/audacity/blob/Audacity-3.7.9/src/menus/EditMenus.cpp).
- Current fork integration overview: [`src/alicorn/README.md`](https://github.com/samanshaiza004/audacity/blob/main/src/alicorn/README.md).
