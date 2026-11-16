# Acetate Agent Guide

## Mission

Build an editing-first MVP GUI for Audacity 3.7.9 in a sibling repository named Acetate. Preserve Audacity's audio engine, project model, import/export, editing semantics, undo history, and recovery. Replace the wxWidgets-authored interface incrementally with Alicorn; do not treat the existing toolbar experiment as a completed GUI migration.

The MVP target is: open one mono/stereo file, display a waveform and time ruler, select a range, play/stop, delete, undo/redo, save/reopen `.aup3`, and export WAV. See `docs/research/audacity-mvp-gui-proposal.md` for exclusions, safety rules, and acceptance gates.

## Current boundary and state

- Audacity's existing C++ application/backend remains authoritative for projects, audio samples, selection, transport, edits, undo/redo, file I/O, export, and recovery.
- Caliber carries bounded, versioned application commands and revision-tagged state where the Odin backend has taken over. Do not duplicate policy in the UI or claim that the full backend rewrite is complete.
- Alicorn owns UI description, layout, interaction, semantics, transient viewport state, and paint.
- wxWidgets remains the native host/platform adapter during migration. Do not start a nested event loop or run UI work on an audio/worker thread.
- The current opt-in Alicorn integration covers selected toolbar controls, the Selection Toolbar's retained numeric-time presentation and key routing for digit focus/editing, bounded waveform geometry including high-zoom connected sample lines, point markers and background fills, range-drag routing and transient snap-guide overlays, selected-range edge strokes, ruler ticks/labels, the track-panel playhead, retained ruler pointer gestures for creating, edge-resizing, and moving loop regions, middle-button pan gestures whose deltas are applied by Audacity's `Viewport`, and wheel events captured through a hidden retained waveform `Scroll_Region` before Audacity applies its existing viewport policy. Alicorn handles Left/Right/Home/End focus navigation and routes digit, Backspace, Delete, Up, and Down edit intents through the hosted key callback; `NumericTextCtrl` still applies those edits through its existing formatter/converter and remains authoritative for values, format changes, native accessibility, and the format menu. wx paints these fields as a fallback. Draggable sample markers use Alicorn's compact six-vertex point-marker payload; Audacity still owns sample hit-testing and editing, and the C++ paint callback still presents retained output through wx. The Snap checkbox and individual action controls use Alicorn's `Hosted_Runtime` for single-surface lifecycle, resize, input, stepping, and paint acknowledgement. The timeline still uses its direct Runtime wrapper, but binds disjoint ruler, waveform-geometry, and waveform-label outputs and acknowledges each exact revision after its synchronous wx paint callback succeeds. No Acetate surface uses `Sdl_GPU_Presenter`. The ruler and waveform share one lazily created Alicorn runtime owned by each `ProjectWindow`; each toolbar control has its own runtime. wx owns the native window/event loop, applies Audacity's existing zoom/scroll policy and handles remaining ruler input, accessibility/platform host, and much of track presentation. This partial migration is not a window-wide runtime or a completed MVP workflow.

- The Zoom tool also routes its pointer gesture through Alicorn; Audacity still owns zoom policy and `ViewInfo`, while wx paints the dashed zoom-region preview.

## Implementation rules

1. Read `README.md`, `CALIBER.md`, the MVP proposal, and the local build instructions before making architectural changes.
2. Inspect current source, branch, remotes, and local modifications first. Preserve work from other agents; never reset or overwrite another contributor's changes.
3. Keep each slice reviewable and behavior-preserving. Prefer existing Audacity and Alicorn public facilities over parallel UI, audio, project, or rendering stacks.
4. Do not copy complete PCM buffers into UI state. Reuse Audacity's visible-range waveform summaries and request only bounded display data.
5. Keep the UI non-authoritative for project data, samples, selection, and history. Identify backend operations that are missing instead of reimplementing their semantics in Alicorn.
6. Never save an unsupported project representation in a way that drops data. Route unsupported project content to the complete Audacity workspace.
7. Preserve the Alicorn revision pin. If a temporary pin change is necessary for a local build, restore it before committing and record the actual tested revision.
8. Don't add or run tests unless the task explicitly asks for tests. Build/compile checks are appropriate for implementation validation; report exactly what ran and its result.
9. Before filing Alicorn issues, check existing issues, especially #196 (completed host-owned Runtime API), #162 (completed renderer-neutral presentation), #164 (completed retained-geometry/cache experiment; concluded no new GPU-canvas API is needed), and #204 (swapchain-safe presenter failure recovery). File only concrete missing capabilities that are not already tracked.
10. Keep application changes in Acetate. Do not push changes to Audacity upstream or the original Audacity repository.

## Coordination

- The integration owner coordinates shared files and publishes to Acetate `main`.
- Agents should take disjoint, explicitly assigned file or feature scopes. Before editing shared CMake, adapter, or host-lifecycle code, tell the integration owner what files and interfaces are needed.
- Communicate verified defects, ABI/pin mismatches, build failures, data-loss risks, and cross-agent dependencies promptly. Report changed files and validation evidence at handoff.
- Do not independently push, rebase shared branches, or change repository remotes.

## Validation and publication

- Standard Audacity build instructions are in `BUILDING.md`.
- Alicorn controls are opt-in via `-Daudacity_use_alicorn_snap_ui=ON`; see `README.md` and `src/alicorn/README.md` for the exact source pin and Odin requirements.
- Caliber is a separate opt-in path; see `CALIBER.md`.
- Run the narrowest relevant build target, then the documented broader validation when requested and available. Native builds do not establish GPU pixels, real audio behavior, OS IME, screen-reader behavior, or project durability by themselves.
- Check `git status` and `git remote -v` before commit/push. The repository's intended publication branch is `main`; verify `origin` points to Acetate before pushing.
