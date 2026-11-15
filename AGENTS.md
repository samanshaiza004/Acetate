# Acetate Agent Guide

## Mission

Build an editing-first MVP GUI for Audacity 3.7.9 in a sibling repository named Acetate. Preserve Audacity's audio engine, project model, import/export, editing semantics, undo history, and recovery. Replace the wxWidgets-authored interface incrementally with Alicorn; do not treat the existing toolbar experiment as a completed GUI migration.

The MVP target is: open one mono/stereo file, display a waveform and time ruler, select a range, play/stop, delete, undo/redo, save/reopen `.aup3`, and export WAV. See `docs/research/audacity-mvp-gui-proposal.md` for exclusions, safety rules, and acceptance gates.

## Current boundary and state

- Audacity's existing C++ application/backend remains authoritative for projects, audio samples, selection, transport, edits, undo/redo, file I/O, export, and recovery.
- Caliber carries bounded, versioned application commands and revision-tagged state where the Odin backend has taken over. Do not duplicate policy in the UI or claim that the full backend rewrite is complete.
- Alicorn owns UI description, layout, interaction, semantics, transient viewport state, and paint.
- wxWidgets remains the native host/platform adapter during migration. Do not start a nested event loop or run UI work on an audio/worker thread.
- The current opt-in Alicorn integration covers selected toolbar controls, bounded waveform geometry and range-drag routing, selected-range edge strokes, ruler ticks/labels, and the track-panel playhead. wx still owns the window/event loop, ruler gestures, backgrounds, and most editor presentation; these view slices use separate Alicorn runtimes. The shared project-window runtime and MVP end-to-end flow are not yet implemented.

## Implementation rules

1. Read `README.md`, `CALIBER.md`, the MVP proposal, and the local build instructions before making architectural changes.
2. Inspect current source, branch, remotes, and local modifications first. Preserve work from other agents; never reset or overwrite another contributor's changes.
3. Keep each slice reviewable and behavior-preserving. Prefer existing Audacity and Alicorn public facilities over parallel UI, audio, project, or rendering stacks.
4. Do not copy complete PCM buffers into UI state. Reuse Audacity's visible-range waveform summaries and request only bounded display data.
5. Keep the UI non-authoritative for project data, samples, selection, and history. Identify backend operations that are missing instead of reimplementing their semantics in Alicorn.
6. Never save an unsupported project representation in a way that drops data. Route unsupported project content to the complete Audacity workspace.
7. Preserve the Alicorn revision pin. If a temporary pin change is necessary for a local build, restore it before committing and record the actual tested revision.
8. Don't add or run tests unless the task explicitly asks for tests. Build/compile checks are appropriate for implementation validation; report exactly what ran and its result.
9. Before filing Alicorn issues, check existing issues, especially #196 (existing host event loop), #162 (renderer-neutral presentation), and #164 (custom GPU canvas/waveform). File only concrete missing capabilities that are not already tracked.
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
