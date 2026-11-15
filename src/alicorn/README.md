# Alicorn-backed Audacity controls

This directory contains small Alicorn-backed controls hosted by Audacity 3.
The opt-in integration renders normal-zoom waveform summary strokes, the
track-panel playback indicator, and time-ruler marks/labels through
Alicorn-retained geometry and text, alongside
the Snap checkbox, Audio Setup toolbar button, Selection Toolbar mode-setup
button, Edit toolbar buttons, Cut/Copy/Paste toolbar buttons, and momentary Stop
transport button. Play remains native because its Shift/Ctrl modifier modes
and toggle presentation need a behavior-preserving adapter. Audacity still owns
the native window, event loop, accessibility bridge, menus, audio-device state,
selection model, command dispatch, ruler formatting, and drawing surface;
Alicorn supplies retained control descriptions, paint commands, ruler
presentation, and transient pointer-drag endpoints for plain audio-track range
selection. This adapter does not create an SDL window or renderer.

The waveform path reads Audacity's existing 256-column min/max/RMS cache,
updates one retained geometry surface owned by each TrackPanel, and translates
geometry paint payloads synchronously through wxGraphicsContext. Audacity's
selected pixel interval is used to append boundary strokes to that bounded
geometry batch; the project selection and wx selection fill remain authoritative.
The same surface draws the playback indicator after the cached track backing
bitmap is presented; Audacity's playback scroller remains authoritative for its
position and scheduling. The wx indicator remains the fallback. The ruler
path takes an owned snapshot of Audacity's generated tick positions, labels,
fonts, and colors, then renders those marks and labels through Alicorn in the
existing wx paint pass. Ruler gestures, selection/play-region backgrounds,
overlays, and the timeline's native host remain wx-owned. Both paths fall back
to the original wx drawing when the Alicorn view cannot be initialized or
updated. The wx `SelectHandle` still applies track eligibility, snapping, time
conversion, selection mutation, undo history, and cancel rollback. The project
selection is the source of truth; Alicorn retains only the active pointer
gesture. The wx track panel still owns the waveform backing surface, selection
background, envelope controls, clip decorations, and high-zoom sample editing.
The geometry bridge's repaint cost has not yet been benchmarked against the
native drawing paths.

These are migration slices, not the target host architecture: each control or
surface currently owns a small retained runtime. A single Alicorn runtime per
project window, hosted by Audacity's existing event loop, remains unimplemented.

## Build

Compile the Alicorn package in this directory as a static library with the
external Alicorn repository root mapped as the collection (the import selects
`runtime`), for example:

```sh
odin build src/alicorn \
  -collection:alicorn=/path/to/alicorn \
  -build-mode:lib -no-entry-point \
  -out:libaudacity_alicorn_snap_view.a
```

The views use Alicorn's `runtime_set_viewport` and `paint_display_list` APIs.
The source checkout must provide those public APIs and Alicorn's bundled font
assets at compile time. Each hosted control currently owns a small retained
runtime; sharing a host session is a separate integration step.

## C ABI and ownership

See [`AudacitySnapView.h`](AudacitySnapView.h) for the exported names, fixed
integer constants, and draw-command layout. The Snap and action views are
opaque, caller-owned handles. All operations for one handle must be serialized
on the same thread. Destroy a handle exactly once, after any paint callback
returns. The action view copies its localized UTF-8 label during creation.

`audacity_alicorn_snap_view_update` takes the current application-owned checked
value. It describes or flushes a retained frame and reports an activation with
`out_changed` and `out_checked`. The host remains authoritative: write the
reported value to `ProjectSnap`, then call update with the resulting state.
Pointer coordinates use Alicorn logical client units. The key vocabulary
includes Tab, Shift+Tab, Space, and an explicit activation request.

`audacity_alicorn_snap_view_paint` consumes the last completed Alicorn display
list synchronously. Its command pointer is stack-backed and valid only during
the callback. `text_utf8` is a borrowed, length-delimited view into runtime
storage, not a NUL-terminated string; copy it if it must outlive the callback.
The callback must not reenter or destroy the view. Surface colors are Alicorn
linear color values; geometry, clip, and translation are logical coordinates.
This adapter emits only the checkbox's surface, check icon, and label text
commands, or the action button's surface and label; unsupported paint payloads
are skipped.

## Accessibility boundary

The retained Alicorn controls provide the visual and input behavior. Audacity's
wx host objects remain the platform-facing accessible objects: Snap exposes a
checkbox role/value, while Audio Setup exposes a push-button role and localized
name. The wx bridge synchronizes their state with `ProjectSnap` and the native
audio menu. Native screen-reader behavior has not yet been manually validated.

The waveform view is owned by `TrackPanel` and serializes create, resize, update,
pointer, paint, and destroy on the wx UI thread. Its geometry surface opts into
Alicorn pointer hit testing; pointer input returns a drag phase and panel-local
anchor/current x coordinates. `SelectHandle` maps the current endpoint through
Audacity's existing view and snap policy before updating the authoritative
selection. Update copies the caller's bounded segment array into retained
Alicorn geometry. Paint visits the completed geometry payload synchronously;
segment storage is borrowed only for the callback. This path does not create an
Alicorn GPU renderer and does not replace Audacity's waveform background,
envelope controls, clip decorations, or high-zoom sample editing.

The ruler view is owned by `AdornedRulerPanel` and also serializes its lifecycle
on the wx UI thread. `Ruler::GetDisplaySnapshot` copies the already-generated
Audacity ruler marks and formatted labels; it does not reimplement time-format
or tick-spacing policy. Alicorn retains the tick geometry and text description,
while wx maps the resulting paint commands onto the existing backing DC. The
snapshot is copied during update. The original `Ruler::Draw` remains the
fallback. No Alicorn code handles ruler gestures or selection/play-region
overlays.
