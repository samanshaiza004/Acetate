# Replacing the wx UI with Alicorn: remaining surfaces and order

Goal: everything wxWidgets draws or manages that is not the native window/event loop moves to Alicorn, with Audacity's C++ code staying authoritative for project, selection, transport, edits and settings. wx stays as the host (window, event translation, native dialogs and OS services) until the host swap. This is an inventory and ordering, not a status claim; sizes are source line counts from 2026-11-17 and are only a rough effort signal.

## Already on Alicorn (opt-in build)

The Time toolbar's audio-position clock, the record and playback meters, both scrollbars, the status bar and the tempo/time-signature fields, the snap-unit chooser, play-at-speed slider, Device toolbar choices and toolbar chrome (grabbers, backgrounds, dock lines), an in-window menu strip, track headers and the waveform vertical ruler (title bar glyphs and text, Mute/Solo/Effects, gain and pan sliders, background; sync-lock icon still wx); Transport, Edit, Cut/Copy/Paste, Tools, File Actions and Snapping action buttons; the Snap checkbox; Selection toolbar numeric fields, format chooser and mode popup; ruler, waveform, selection fills, playhead, loop region, snap guides, zoom preview and clip titles through one retained runtime per project window.

## Still wx-drawn or wx-managed

| Area | Where | Size | Notes |
| --- | --- | --- | --- |
| Track header remnants: sync-lock icon (theme bitmap), keyboard-focus ring, drag-to-reorder affordances | `tracks/ui/CommonTrackInfo.cpp`, `TrackInfo.cpp` | small | The title bar, buttons, sliders and background moved to a retained `Track_Controls_View` (first slice). Theme bitmap icons are replaced by vector glyphs; pixel parity with the stock bevel buttons is not claimed. |
| Vertical ruler: zoom-drag preview, other scale modes | `tracks/ui/ChannelVRulerControls.cpp`, `widgets/Ruler.cpp` | small | The waveform-scale ruler axis, ticks and labels now use the retained track-controls scene; the dashed zoom preview and non-waveform modes are unchecked. |
| Scrollbar arrow buttons and accessibility, panel backgrounds | `ProjectWindow.cpp`, `widgets/AlicornScrollBar.cpp` | small | Both scrollbars (thumb, track, thumb drag, track paging) and the status-bar text now paint through retained scenes; the native scrollbar remains the non-Alicorn fallback. Line-step arrows and a screen-reader value/role are not implemented. |
| Tempo/time-signature leftovers: static labels, text caret blink, IME | `toolbars/TimeSignatureToolBar.cpp`, `widgets/AlicornFields.cpp` | small | The tempo and upper-signature spin fields and the lower-signature drop-down now paint through retained scenes (`AlicornSpinField`, `AlicornChoiceField`). Number entry is handled in the control (no native text input), so there is no IME composition path; the caret does not blink and the "Tempo"/"Time Signature" labels are still wx. |
| Spectral selection bar | `toolbars/SpectralSelectionBar.cpp` | ~0.5k | Same one-line opt-in as the Time toolbar (`AlicornPaint(true)`), not applied because it could not be opened and checked in this session. |
| Meter remnants: Mixer-board meter style, level slider interaction | `widgets/MeterPanel.cpp`, `toolbars/MeterToolBar.cpp` | small | Meter bars, clip indicators, strokes, ruler and slider now paint through a retained scene (`MeterPanel::PaintRetained`) on the UI thread from the values the existing timer already copies; the audio thread is untouched. |
| Transcription toolbar buttons and the Device toolbar's mic/speaker icons | `toolbars/TranscriptionToolBar.cpp`, `DeviceToolBar.cpp` | small | The Device toolbar's four choice controls (host, recording device, channels, playback device) now use `AlicornChoiceField`. The Transcription toolbar's `AButton` array (including the visible play-at-speed button) and the two `AStaticBitmap` icons remain wx. |
| Toolbar docking logic, floating frames' native window, drag indicator | `toolbars/ToolDock.cpp`, `ToolManager.cpp`, `ToolBar.cpp` | ~3.6k | The toolbar backgrounds, drag grabbers, resize gutters, the dock's gap lines and the floating frame's border and size grip now paint through per-window retained scenes (`AlicornChrome`). Docking, drag and resize behavior is unchanged wx code, floating `ToolFrame` is still a native frame (host-swap work), and the shaped drag indicator still paints with wx. |
| Context menus (16 callers), Alt mnemonics, menu accessibility | `widgets/BasicMenu.cpp`, `widgets/AlicornMenu.cpp`, `PopupMenuTable`, track/clip menu callers | large | `AlicornMenu` is a non-blocking menu session that owns a plain-data model, the selection and keyboard state, focus restoration and journal record/replay; the menu strip uses it, and the Meter context menu is migrated onto it. The remaining callers still use the synchronous native `PopupMenu` through `BasicMenu::Handle::Popup`, because their stack-local command data must first become owned data. The native bar stays attached, so Alt mnemonics and screen-reader semantics are unchanged from native. |
| Dialogs (`ShuttleGui`, ~170 files; ~100 `wxDialog` users) | everywhere | very large | Preferences, export, import options, effect UIs. Highest effort; route by usage frequency, keep native file pickers. |
| Track panel remnants: sync-lock tiling, highlit (hover) clip buttons, in-place title editing, scrub text | `TrackArt.cpp`, `TextEditHelper.cpp`, `ScrubUI.cpp` | small | Clip affordance outlines, folded clips and the lowlit overflow/pitch/speed buttons now go through one retained scene per track affordance row (with a rerun-in-wx fallback). The hover-state buttons, the title text editor, sync-lock tiling and scrub speed text remain wx; the pitch and speed icons are simplified vector glyphs. |

## Order

1. Track header and vertical ruler (done for the waveform view; see validation).
2. Remaining toolbar controls: the Transcription toolbar buttons and the Device toolbar icons (clock, meters, tempo/time signature, snap units, speed slider and device choices done).
3. Panel backgrounds (scrollbars and status bar done; see validation).
4. Transcription toolbar buttons (Device toolbar choices and dock chrome done).
5. Menu strip and the async `AlicornMenu` session done; migrate context-menu callers one at a time (Meter done), then retire the synchronous `BasicMenu::Handle::Popup`.
6. Dialogs, by frequency: Export, Import, Preferences, then the rest.
7. Host swap: remove wxWidgets from window creation and event translation (needs native accessibility, IME and file-dialog bridges).

## Rules that keep each slice safe

- One retained output or hosted control per slice, with a wx fallback behind the same opt-in flag until the slice is verified.
- Alicorn owns presentation, layout, interaction and semantics; Audacity code keeps state and policy. If a backend operation is missing, record it instead of reimplementing it in the UI.
- Verify each slice in a running build (pixels, input, resize, teardown) before moving on; compile success alone is not verification.
- Needs Alicorn side: none confirmed. Slice 1 may need an image/icon draw primitive and slider geometry; check existing issues before reporting.
