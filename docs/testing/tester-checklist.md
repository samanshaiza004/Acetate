# Acetate test-build checklist

For people trying a build from the **Acetate test builds** GitHub Action
(`Actions` → `Acetate test builds` → `Run workflow`, then download the artifact
from the finished run). These are unsigned test builds of the opt-in Alicorn
configuration, not releases.

Scope is the MVP workflow in
[`docs/research/audacity-mvp-gui-proposal.md`](../research/audacity-mvp-gui-proposal.md):
open one mono or stereo file, see a waveform against a time ruler, select a
range, play and stop, delete, undo and redo, save and reopen an `.aup3`, and
export WAV. Anything beyond that is expected to look and behave like normal
Audacity 3 (wxWidgets); report it only if it is broken.

**What has been verified, and where.** Only macOS on Apple Silicon has been
driven by hand and by script, and only the items marked ✅ below. The Windows
build has not been run by anyone yet, so every Windows item is open. Items
marked ❓ have never been checked on any platform. Please mark each item
✅ works, ❌ broken (say how), or ➖ not tried.

## 0. Install and first launch

**macOS (Apple Silicon only).** Unzip `Acetate-macos-arm64.zip`. The app is
ad-hoc signed, not notarized, so run `xattr -cr Audacity.app` in the folder
that contains it, then open it (right-click → Open the first time).

**Windows (x64).** Unzip `Acetate-windows-x64.zip` to a normal folder (not a
protected one) and run `Audacity.exe`. SmartScreen may warn about an unknown
publisher.

- [ ] The app launches and reaches a window without crashing.
- [ ] First launch with no existing Audacity settings works (on macOS, a
      first-run welcome dialog, a recovery dialog if you have old unsaved
      projects, and a mostly blank "New Plugins" window may appear; note what
      you see). ✅ macOS: no crash.
- [ ] It does not hang on "Searching for plugins". Third-party plugin hosting
      (Audio Unit, VST3, LADSPA, LV2) is deliberately not built.
- [ ] Which toolbars appear, and whether they match the screenshots in the
      run summary or earlier reports.
- [ ] If you already use Audacity: this build shares your settings and its
      recovery list with it (macOS: `~/Library/Application Support/audacity`).
      Use a scratch account or back that folder up first.

## 1. Import and view (acceptance gate 1)

- [ ] Import a **stereo** WAV (File → Import → Audio, or the *Import Audio*
      button). Duration and channel count are right. ✅ macOS
- [ ] Import a **mono** file. ❓
- [ ] Import at least one other format (MP3, FLAC, OGG) and a different sample
      rate. ❓
- [ ] The time ruler shows tick marks and readable labels (0.0, 1.0, …) and
      they change when you zoom. ✅ macOS (labels clip at narrow widths? note
      it)
- [ ] The clip title appears in the clip header, not in the ruler. ✅ macOS
- [ ] The waveform has no faint vertical lines or gaps at regular intervals.
      ✅ macOS
- [ ] Compare against File → Open in a stock Audacity 3.7: the waveform shape
      and levels look the same.
- [ ] Zoom In, Zoom Out, Zoom to Selection, Fit to Width, Zoom Toggle (toolbar
      and menu) all work; the ruler follows.
- [ ] Scroll with the mouse wheel / trackpad, with a middle-button drag (if you
      have one), and with the scrollbar; the ruler and waveform stay aligned.
- [ ] Resize the window, maximize, and move it between displays or DPI scales.
      The ruler, waveform, toolbars and selection fill stay correct. ❓
- [ ] Open a project with **two or more tracks**: it should fall back to the
      normal Audacity timeline, not show a broken Alicorn one. ❓

## 2. Selecting a range (gate 2)

- [ ] Drag across the waveform left→right and right→left; the shaded range and
      the Selection bar's start and end follow. ✅ macOS
- [ ] Drag while the view is scrolled and zoomed; the endpoints are the times
      you expect. ❓
- [ ] Click once to place a cursor; the Selection bar shows start = end.
- [ ] Click a digit in the Selection bar and type a number; the range and the
      shading update. Up/Down step the digit that follows the one you typed.
      Left/Right/Home/End move between digits. ✅ macOS
- [ ] The format chevron opens a format chooser; pick another format (e.g.
      samples, hh:mm:ss) and check the display and typing still work. ❓
- [ ] The selection-mode button next to the fields opens a popup
      (Start/End, Start/Length, Length/End, Length/Center); each mode behaves.
      ❓ (a crash in this popup was fixed; popup appearance and keyboard use
      were never checked)
- [ ] Snap checkbox and its units snap a drag when on. ❓
- [ ] **Keyboard in the track panel** with a physical keyboard: click the
      waveform, then use Left/Right (cursor), Shift+Left/Right (extend),
      Ctrl+Shift+Left/Right (contract), Home/End. Home and End worked;
      the arrow keys did **not** respond to synthetic key events in any build,
      so this needs a real keyboard. ❓
- [ ] Tab moves keyboard focus through toolbars and back to the track panel,
      with a visible focus indicator.

## 3. Playback (gate 3)

Use headphones or turn the volume down first.

- [ ] Play starts audio from the cursor or selection; Stop stops it and the
      clock returns. ✅ macOS (audio output itself was not listened to)
- [ ] You hear the file, correct pitch and speed, no clicks or dropouts. ❓
- [ ] A vertical playhead line moves through the tracks and a marker moves in
      the ruler, in time with the audio, and the Audio Position clock advances.
      ✅ macOS
- [ ] Pause and resume; Skip to Start; Skip to End; Loop. ❓
- [ ] Shift-click and Ctrl-click Play (play variants) behave like Audacity. ❓
- [ ] Playing a selection stops at its end.
- [ ] The playhead line does not flicker, smear, or leave trails; CPU stays low
      while idle. ❓

## 4. Editing (gate 4)

- [ ] Select a range and press the toolbar **Delete**; the audio shortens by
      that amount and the waveform updates. ✅ macOS
- [ ] **Undo** and **Redo** from the toolbar and from Edit/⌘Z/Ctrl+Z restore
      and reapply it. ✅ macOS
- [ ] Trim, Silence, Cut, Copy and Paste from the toolbar work. ❓
- [ ] The Tools toolbar switches between Selection, Envelope, Draw and
      Multi-Tool and the pressed button follows the active tool. ❓
- [ ] With the Zoom tool, dragging a region shows a dashed preview and zooms.
      ❓
- [ ] Closing the project with unsaved edits asks whether to save. ✅ macOS
- [ ] The Edit History window lists your edits sensibly. ❓

## 5. Save, reopen, recover (gate 5)

- [ ] Save Project (button and File menu) writes an `.aup3`. ✅ macOS
- [ ] Quit, relaunch, open the `.aup3`: same audio, same length. ✅ macOS
- [ ] Undo history after reopening starts fresh (this matches stock Audacity).
- [ ] Kill the app (Force Quit / Task Manager) during an edit, relaunch, and
      use Automatic Crash Recovery: the edited project comes back. ❓
- [ ] Open a project that contains things the MVP cannot show (labels, MIDI,
      many tracks): it must open in the normal Audacity view and not lose
      data. ❓

## 6. Export (gate 6)

- [ ] The **Export Audio** button opens the Export dialog with sensible
      defaults (WAV, entire project). ✅ macOS
- [ ] Export a WAV; open it in another tool. Channel count, sample rate and
      duration match; the audio matches the source (bit-identical for an
      unedited 16-bit file). ✅ macOS
- [ ] Export after a delete, after Undo, and with *Current Selection*. ❓
- [ ] Other formats you care about (FLAC, MP3, OGG). ❓

## 7. Appearance, input and accessibility (gate 8)

- [ ] Buttons look and hover/press correctly; disabled buttons look disabled
      (for example Undo with nothing to undo).
- [ ] Tooltips appear on toolbar buttons and name the action.
- [ ] Dark mode / light mode and any high-contrast setting: nothing becomes
      unreadable. ❓
- [ ] Text is crisp on a high-DPI display and not clipped at the window edge.
- [ ] Screen reader (VoiceOver on macOS, NVDA or Narrator on Windows): toolbar
      buttons have names, the Selection bar exposes "Start …" / "End …"
      values, and the Delete/Undo/Redo/Play buttons can be pressed. ✅ macOS
      accessibility tree only; not tested with a screen reader. ❓
- [ ] Right-click menus, the menu bar, and dialogs (Preferences, Export,
      Import) are the normal Audacity ones and behave.
- [ ] Text-entry and IME: type in the Selection bar and in dialog fields using
      your normal input method. ❓

## 8. Stability

- [ ] Use it for ten minutes of normal editing, with several imports,
      deletes and undos. No crash, hang, or growing delay.
- [ ] Quit normally; relaunch. No crash on exit, no stale recovery entries from
      a clean exit.
- [ ] Open and close several project windows (File → New).

## Known limitations (not bugs to report)

- Single-track MVP: multitrack, recording, effects UIs, labels and clip
  movement are still the stock Audacity interface.
- Audio Unit, VST3, LADSPA and LV2 plugins are not available.
- Only Apple Silicon macOS and Windows x64 are built.
- The build is unsigned and not notarized.
- Pixel-level parity with stock Audacity, repaint cost and real audio quality
  have not been measured.

## What to send back

For every ❌ or surprise, please include:

- the build: the `BUILD-INFO.txt` next to the zip (commit and Alicorn pin)
- OS and version, CPU, display scale, and whether a screen reader was running
- the exact steps, what you expected, and what happened
- a screenshot or short screen recording
- for a crash: the crash report (macOS: Console → Crash Reports; Windows:
  Event Viewer → Application) and the log at
  `Help → Diagnostics → Show Log…`
