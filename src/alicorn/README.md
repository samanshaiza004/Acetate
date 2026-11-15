# Alicorn-backed Audacity controls

This directory contains opt-in Alicorn views hosted by Audacity 3. The
integration renders selected toolbar controls (including Pause/Play/Stop and
Skip to Start/End,
modifier-specific Play modes, and the Edit toolbar's Delete/Undo/Redo actions) and
a timeline containing the normal-zoom waveform, time-ruler ticks and labels,
selection/background fills, selected-range edge strokes, and playback
indicator. It does not create an Alicorn window or GPU renderer.

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
retained geometry surface. This uses thin segment strokes as a temporary fill
encoding; the C++ paint callback still maps the retained geometry through wx
`wxGraphicsContext`, so this is not yet a renderer migration. The native path
remains the fallback for sync-lock patterns, envelope highlighting, high-zoom
sample drawing, and any rejected geometry batch. Alicorn #200 tracks a
first-class retained rectangle batch to replace the fill encoding. The playback
indicator uses the same retained geometry surface after the cached track
backing bitmap is presented; Audacity's playback scroller remains authoritative.
The ruler adapter copies Audacity's generated tick positions, formatted labels,
fonts, and colors into the same retained tree. It does not reimplement ruler
policy.

Alicorn hit testing routes plain audio-track range drags. wx `SelectHandle`
continues to enforce track eligibility, snapping, time conversion, selection
mutation, undo history, and cancel rollback. Project selection is authoritative;
Alicorn retains only the active pointer gesture. Selection-mode calculations are
also exposed through `SelectionBar::ApplyTimeControlValue`, independent of the
current wx numeric editor, so a future Alicorn editor can reuse Audacity's
Start/End, Start/Length, Length/End, and Length/Center semantics. The existing
`NumericTextCtrl` remains active because it supplies segmented time editing,
format switching, native text behavior, and accessibility that the current
Alicorn bridge does not yet replace. Keep it until the host can deliver
committed text, preedit/cancel composition, editing and navigation keys, focus
lifecycle, and accessible labels/values; Alicorn #196 tracks that host contract.
This avoids replacing it with a plain text field that silently drops these
behaviors. wx still owns ruler gestures,
play-region and blank-track backgrounds, sync-lock selection patterns, the
track backing surface, envelope controls, clip decorations, high-zoom sample
editing, accessibility host objects, and native audio services. Delete/Undo/Redo
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
