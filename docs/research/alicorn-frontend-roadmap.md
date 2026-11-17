# Replacing the wx UI with Alicorn: remaining surfaces and order

Goal: everything wxWidgets draws or manages that is not the native window/event loop moves to Alicorn, with Audacity's C++ code staying authoritative for project, selection, transport, edits and settings. wx stays as the host (window, event translation, native dialogs and OS services) until the host swap. This is an inventory and ordering, not a status claim; sizes are source line counts from 2026-11-17 and are only a rough effort signal.

## Already on Alicorn (opt-in build)

The Time toolbar's audio-position clock, track headers and the waveform vertical ruler (title bar glyphs and text, Mute/Solo/Effects, gain and pan sliders, background; sync-lock icon still wx); Transport, Edit, Cut/Copy/Paste, Tools, File Actions and Snapping action buttons; the Snap checkbox; Selection toolbar numeric fields, format chooser and mode popup; ruler, waveform, selection fills, playhead, loop region, snap guides, zoom preview and clip titles through one retained runtime per project window.

## Still wx-drawn or wx-managed

| Area | Where | Size | Notes |
| --- | --- | --- | --- |
| Track header remnants: sync-lock icon (theme bitmap), keyboard-focus ring, drag-to-reorder affordances | `tracks/ui/CommonTrackInfo.cpp`, `TrackInfo.cpp` | small | The title bar, buttons, sliders and background moved to a retained `Track_Controls_View` (first slice). Theme bitmap icons are replaced by vector glyphs; pixel parity with the stock bevel buttons is not claimed. |
| Vertical ruler: zoom-drag preview, other scale modes | `tracks/ui/ChannelVRulerControls.cpp`, `widgets/Ruler.cpp` | small | The waveform-scale ruler axis, ticks and labels now use the retained track-controls scene; the dashed zoom preview and non-waveform modes are unchecked. |
| Scrollbars, status bar, panel backgrounds | `ProjectWindow.cpp`, `ProjectStatus.cpp` | ~1.4k | Scrollbar thumb geometry/policy stays in `Viewport`. |
| Tempo / time signature toolbar | `toolbars/TimeSignatureToolBar.cpp` | ~0.3k | Needs Alicorn text field + stepper + select. |
| Spectral selection bar | `toolbars/SpectralSelectionBar.cpp` | ~0.5k | Same one-line opt-in as the Time toolbar (`AlicornPaint(true)`), not applied because it could not be opened and checked in this session. |
| Meters and level sliders | `widgets/MeterPanel.cpp`, `toolbars/MeterToolBar.cpp` | ~2.8k | Custom-painted, real-time; needs a bounded meter payload from the audio thread without running UI there. |
| Device, transcription, mixer toolbars | `toolbars/DeviceToolBar.cpp`, `TranscriptionToolBar.cpp` | ~2.0k | Selects, slider, buttons. Blocked in part by Alicorn grouped options (#198). |
| Toolbar docking chrome, grabbers, floating frames | `toolbars/ToolDock.cpp`, `ToolManager.cpp`, `ToolBar.cpp` | ~3.6k | Last toolbar step; floating `ToolFrame` is a native window and belongs with the host swap. |
| Menu bar and context menus | `MenuRegistry`, `commands/*` | large | Native menus; replacing them needs an Alicorn menu surface plus keyboard/accelerator and screen-reader parity. |
| Dialogs (`ShuttleGui`, ~170 files; ~100 `wxDialog` users) | everywhere | very large | Preferences, export, import options, effect UIs. Highest effort; route by usage frequency, keep native file pickers. |
| Track panel remnants | `TrackPanel.cpp`, handles | ~1.7k | Sync-lock pattern, blank-track backgrounds, outlines, affordance buttons, in-place title editing. |

## Order

1. Track header and vertical ruler (done for the waveform view; see validation).
2. Remaining default toolbars: tempo/time signature, time, meters and sliders.
3. Scrollbars, status bar and panel backgrounds.
4. Device/transcription toolbars, then dock chrome.
5. Menu bar and context menus.
6. Dialogs, by frequency: Export, Import, Preferences, then the rest.
7. Host swap: remove wxWidgets from window creation and event translation (needs native accessibility, IME and file-dialog bridges).

## Rules that keep each slice safe

- One retained output or hosted control per slice, with a wx fallback behind the same opt-in flag until the slice is verified.
- Alicorn owns presentation, layout, interaction and semantics; Audacity code keeps state and policy. If a backend operation is missing, record it instead of reimplementing it in the UI.
- Verify each slice in a running build (pixels, input, resize, teardown) before moving on; compile success alone is not verification.
- Needs Alicorn side: none confirmed. Slice 1 may need an image/icon draw primitive and slider geometry; check existing issues before reporting.
