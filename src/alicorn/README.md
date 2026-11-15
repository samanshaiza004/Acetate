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
fills also use bounded retained rectangles when the complete batch fits; the
zero line remains a wx overlay. The C++ paint callback still presents that
geometry through wx `wxGraphicsContext`, so this is not yet a renderer
migration. wx remains the fallback for sync-lock patterns and geometry batches
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
Length/End, and Length/Center semantics. The existing
`NumericTextCtrl` remains active because it supplies segmented time editing,
format switching, native text behavior, and accessibility that the current
Alicorn bridge does not yet replace. Keep it until the host can deliver
committed text, preedit/cancel composition, editing and navigation keys, focus
lifecycle, and accessible labels/values; Alicorn #196 tracks that host contract.
This avoids replacing it with a plain text field that silently drops these
behaviors. wx still owns the remaining ruler gestures and paints blank-track
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
teardown. Acetate now uses it for the single-surface Snap checkbox: wx still
owns the window and event loop, and synchronously presents the checkbox's
retained commands through `wxGraphicsContext`. This is a narrow lifecycle
adoption, not proof of SDL surface interoperation or GPU presentation. The
timeline still uses its existing runtime wrapper, and Acetate has not adopted
`Sdl_GPU_Presenter`.

Alicorn #196, which introduced the host-owned Runtime API, is complete. The
remaining wx surface and renderer validation belongs to Acetate integration
work and Alicorn #162. In particular, the presenter failure-recovery contract
for acquired swapchain targets is tracked by Alicorn #204. Before moving a
surface to SDL_GPU, Acetate must validate wx/SDL surface compatibility, event
fairness, focus/IME, accessibility, resize/DPI, frame retirement, wake
delivery, and shutdown. Keep wx controls and the wx paint path as fallbacks
until those checks pass.

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
in [`AudacitySnapView.h`](AudacitySnapView.h). The wx host remains authoritative
for control state and platform-facing accessibility objects.
