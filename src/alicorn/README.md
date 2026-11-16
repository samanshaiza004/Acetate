# Alicorn-backed Audacity controls

This directory contains opt-in Alicorn views hosted by Audacity 3. The
integration renders selected toolbar controls (including Pause/Play/Stop and
Skip to Start/End,
modifier-specific Play modes, and the Edit toolbar's Delete/Undo/Redo actions) and
a timeline containing the normal-zoom waveform, time-ruler ticks and labels,
selection/background fills, selected-range edge strokes, playback indicator,
and point-selection caret for selected tracks. It does not create an Alicorn
window or GPU renderer.

## Timeline runtime and ownership

`ProjectWindow` lazily owns one `AlicornTimelineView` for the ruler and
TrackPanel waveform. Its Odin `Timeline_View` contains the retained ruler and
waveform geometry in one Alicorn runtime. The view is destroyed after project
window children are destroyed and before the project is closed. Toolbar
controls still use separate runtimes, so this is a timeline consolidation,
not a window-wide Alicorn host.

The waveform adapter reads Audacity's existing 256-column min/max/RMS cache.
At normal zoom, it also sends bounded per-column envelope/background fill,
zero-line, selected-range fill and edge, and waveform segments through the
retained geometry surface. Fill runs use bounded first-class rectangles rather
than thin-line encoding; Alicorn owns and validates the complete paint payload
and keeps it within one vertex budget. At high zoom, connected sample lines,
clipping marks, and draggable sample points use retained geometry. Point glyphs
use Alicorn's compact six-vertex marker payload rather than its 48-vertex
filled-circle primitive. High-zoom waveform background, envelope, and selection
fills also use bounded retained rectangles when the complete batch fits. The
same bounded background adapter is used for normal-zoom envelope-highlight and
sync-lock-selected cases when that background is painted separately. Successful
background paths draw the zero line through retained geometry after any
sync-lock pattern; wx remains the fallback. Plain solid channel backgrounds and
selected-track selection partitions also use bounded retained fills when no beat
grid is active. Sync-lock-only patterns, beat-grid fills, and non-solid brushes
remain on wx. The C++ paint callback still
presents that geometry through wx `wxGraphicsContext`, so this is not yet a
renderer migration. Waveform clip boundaries and selected-clip strokes also use
retained line segments; the original wx path remains the fallback and continues
to serve spectrum and note tracks. wx remains the fallback for sync-lock patterns and geometry batches
that exceed the surface budget or cannot be submitted. Alicorn #201 tracks the
dense-marker capability; native pixel and repaint-cost acceptance remains
open. The rectangle payload closes the API gap tracked by Alicorn #200. The
playback indicator and
point-selection caret use independent inert retained overlays after the
cached track backing bitmap is presented. Audacity's playback scroller and
selected time remain authoritative. Keeping each indicator in its own owner
means moving or clearing one cannot replace the other indicator or the
retained waveform geometry. wx paints either indicator if the retained path is
unavailable. The ruler adapter copies
Audacity's generated tick positions, formatted labels, fonts, and colors into
the same retained tree, and paints its selected-range and loop-range bands,
loop-boundary markers, and point-selection caret through retained geometry.
The caret is a separate inert layer above ruler labels. Audacity remains
authoritative for those ranges, the cursor time, and their pixel/time mapping.
Range-only repaint updates geometry without rebuilding the ruler description.
It does not reimplement ruler policy.

Alicorn hit testing routes plain audio-track range drags. wx `SelectHandle`
continues to enforce track eligibility, snapping, time conversion, selection
mutation, undo history, and cancel rollback. Project selection is authoritative.
Alicorn routes ruler pointer gestures that create, resize, and move loop regions;
Audacity's `PlayRegionAdjustingHandle` subclasses retain hit-target priority,
snapping, loop-region mutation, selection-sync, and rollback policy. Project
selection and loop-region state remain authoritative; Alicorn retains only active
pointer gestures. Middle-button
horizontal pan deltas are derived from the retained pointer gesture; wx retains
mouse capture and Audacity's `Viewport` applies the horizontal scroll.
Selection-mode calculations are also exposed through
`SelectionBar::ApplyTimeControlValue`, independent of the current wx numeric
editor, so a future Alicorn editor can reuse Audacity's Start/End, Start/Length,
Length/End, and Length/Center semantics. `GetSelectionMode` and
`ApplySelectionMode` expose the selected mode to an adapter without requiring
access to `SelectionBar`'s private state; the latter keeps preference updates,
field labels, numeric formats, and layout in Audacity. The native mode menu
remains in place for now: a control-sized wx surface clips Alicorn's anchored
popup to its own bounds, and Acetate does not yet expose Alicorn popup semantics
through the native accessibility tree. The Selection Toolbar's numeric-time
values now use a retained Alicorn view for segmented digits, formatted labels,
focused-digit appearance, and the format chevron. Alicorn's hosted input owns
Left/Right/Home/End digit-focus navigation and physical pointer hit testing for
digit clicks. `NumericTextCtrl` mirrors the selected digit for Audacity's native
accessibility object and continues to own digit entry, increment/decrement,
format changes, tab traversal, and the context menu. wx still translates native
events and retains its paint path as a fallback. Native text input, IME handling,
and accessibility have not moved to Alicorn, and visual parity has not yet been
checked in a running application. Alicorn #196
delivered the hosted Runtime contract; further editor and host integration
remain Acetate work. wx still owns the remaining ruler gestures and paints blank-track
backgrounds and sync-lock selection patterns. The track panel currently has no
play-region shading; loop-region visualization is on the ruler. wx also paints
the track backing surface, envelope controls,
clip decorations, high-zoom sample editing, accessibility host objects, and
native audio services. Delete/Undo/Redo
buttons dispatch through Audacity's existing command system; this does not yet
prove the complete single-track editing workflow. The original wx waveform,
ruler, and playhead drawing paths remain fallbacks. The normal-zoom fill path
has passed a native compile only; pixel parity, repaint cost, and interaction
behavior have not yet passed the MVP acceptance gates.

All timeline calls are serialized on the wx UI thread. Painting is synchronous:
paint payload and text pointers are borrowed from runtime storage and remain
valid only for the callback. Callbacks must not reenter or destroy the view.
Waveform input and ruler snapshots are copied into Alicorn-owned retained data;
project samples and selection are not copied into a second authoritative UI
model.

The pinned Alicorn revision exposes `Hosted_Runtime`, a nonblocking per-surface
lifecycle for a host-owned event loop, and `Sdl_GPU_Presenter`, which consumes
retained paint commands through a host-owned SDL_GPU command buffer and target.
Hosted_Runtime covers input delivery, resize/DPI, one-shot deadlines, worker
wakes, accessibility snapshots/actions, frame acknowledgement, and ordered
teardown. Acetate uses it for the Snap checkbox and individual action controls,
each hosted on its own wx surface. wx still owns the windows and event loop,
forwards the native input and resize calls, and synchronously presents retained
commands through `wxGraphicsContext`. The timeline uses one retained Runtime
with disjoint ruler and waveform output roots; each synchronous wx paint path
captures and acknowledges that output's exact pending revision after drawing.
This exercises Alicorn [#205's multi-output scheduling contract](https://github.com/samanshaiza004/alicorn/issues/205)
without duplicating the Runtime. It does not prove SDL surface interoperation
or GPU presentation. A native session still needs to confirm independent update
and idle behavior.
Acetate has not adopted `Sdl_GPU_Presenter`.

Alicorn #196, which introduced the host-owned Runtime API, and Alicorn
#162's renderer-neutral presenter are complete. Acetate has not adopted
`Sdl_GPU_Presenter`; its use still depends on the swapchain-safe failure
recovery tracked by Alicorn #204 and Acetate's own native validation of wx/SDL
surface compatibility, event fairness, focus/IME, accessibility, resize/DPI,
frame retirement, wake delivery, and shutdown. Keep wx controls and the wx
paint path as fallbacks until those checks pass.

## Build

The configured Audacity build enables this integration with
`-Daudacity_use_alicorn_snap_ui=ON`. The Alicorn source path must match the
revision in `AlicornSnapUIPin.cmake` and include Odin and Alicorn's font assets.
The CMake module builds the Odin package as a static library and links the C++
wx adapter. See the repository [`BUILDING.md`](../../BUILDING.md) and root
[`README.md`](../../README.md) for the complete command.

## Other controls

The Snap checkbox, Audio Setup button, Selection Toolbar mode-setup button,
Edit toolbar buttons, Cut/Copy/Paste buttons, and Stop button use small opaque
C ABI handles. Their existing contracts and public paint payloads are declared
in [`AudacitySnapView.h`](AudacitySnapView.h). The Selection Toolbar's numeric
time rendering adapter is declared in
[`AudacityNumericTimeView.h`](AudacityNumericTimeView.h). The wx host remains
authoritative for control state and platform-facing accessibility objects.
