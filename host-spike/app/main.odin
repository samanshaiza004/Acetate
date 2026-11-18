package main

// Experimental Alicorn-owned host window for Acetate. Audacity's C++ project
// and track code stays authoritative: this program only asks the backend shim
// (host-spike/backend) to import a file and to summarise the waveform for the
// visible columns, then draws that bounded data through a retained GPU
// surface. It does not copy sample buffers and it never runs audio or backend
// work off the main thread.
import "core:c"
import "core:fmt"
import "core:math"
import "core:os"
import "core:path/filepath"
import "core:strings"
import "core:time"
import alicorn "alicorn:runtime"
import host "alicorn:native/sdl_gpu"
import ui "alicorn:ui"

foreign import backend "system:acetate-host-backend"

Backend :: distinct rawptr

@(default_calling_convention = "c")
foreign backend {
	acetate_backend_abi_version :: proc() -> i32 ---
	acetate_backend_create :: proc() -> Backend ---
	acetate_backend_destroy :: proc(b: Backend) ---
	acetate_backend_import :: proc(b: Backend, path: cstring) -> i32 ---
	acetate_backend_last_error :: proc(b: Backend) -> cstring ---
	acetate_backend_revision :: proc(b: Backend) -> u64 ---
	acetate_backend_track_count :: proc(b: Backend) -> i32 ---
	acetate_backend_channel_count :: proc(b: Backend, track: i32) -> i32 ---
	acetate_backend_track_rate :: proc(b: Backend, track: i32) -> f64 ---
	acetate_backend_track_start :: proc(b: Backend, track: i32) -> f64 ---
	acetate_backend_track_end :: proc(b: Backend, track: i32) -> f64 ---
	acetate_backend_project_end :: proc(b: Backend) -> f64 ---
	acetate_backend_update :: proc(b: Backend) ---
	acetate_backend_play :: proc(b: Backend, t0, t1: f64) -> i32 ---
	acetate_backend_stop :: proc(b: Backend) ---
	acetate_backend_is_playing :: proc(b: Backend) -> i32 ---
	acetate_backend_play_position :: proc(b: Backend) -> f64 ---
	acetate_backend_set_selection :: proc(b: Backend, t0, t1: f64) ---
	acetate_backend_get_selection :: proc(b: Backend, t0, t1: ^f64) ---
	acetate_backend_delete_selection :: proc(b: Backend) -> i32 ---
	acetate_backend_can_undo :: proc(b: Backend) -> i32 ---
	acetate_backend_can_redo :: proc(b: Backend) -> i32 ---
	acetate_backend_undo :: proc(b: Backend) -> i32 ---
	acetate_backend_redo :: proc(b: Backend) -> i32 ---
	acetate_backend_save :: proc(b: Backend, path: cstring) -> i32 ---
	acetate_backend_open :: proc(b: Backend, path: cstring) -> i32 ---
	acetate_backend_export_wav :: proc(b: Backend, path: cstring) -> i32 ---
	acetate_backend_has_unsaved_changes :: proc(b: Backend) -> i32 ---
	acetate_backend_project_path :: proc(b: Backend) -> cstring ---
	acetate_backend_get_view :: proc(b: Backend, left, pixels_per_second: ^f64) ---
	acetate_backend_set_view :: proc(b: Backend, left, pixels_per_second: f64) -> f64 ---
	acetate_backend_zoom_limits :: proc(min_pps, max_pps: ^f64) ---
		acetate_backend_pref_int :: proc(b: Backend, key: cstring, fallback: c.long) -> c.long ---
		acetate_backend_set_pref_int :: proc(b: Backend, key: cstring, value: c.long) ---
		acetate_backend_pref_string :: proc(b: Backend, key: cstring) -> cstring ---
		acetate_backend_set_pref_string :: proc(b: Backend, key, value: cstring) ---
		acetate_backend_recoverable_count :: proc(b: Backend) -> i32 ---
		acetate_backend_recoverable_info :: proc(b: Backend, index: i32, modified_seconds, bytes: ^f64) -> i32 ---
		acetate_backend_recover :: proc(b: Backend, index: i32) -> i32 ---
		acetate_backend_discard_recoverable :: proc(b: Backend, index: i32) -> i32 ---
		acetate_backend_was_recovered :: proc(b: Backend) -> i32 ---
	acetate_backend_waveform :: proc(b: Backend, track, channel: i32, t0, t1: f64, columns: i32, min_max: [^]f32) -> i32 ---
}

EXPECTED_ABI :: 7
DEFAULT_WINDOW_WIDTH :: 1100
DEFAULT_WINDOW_HEIGHT :: 640
MAX_RECOVERABLES_SHOWN :: 5
SIZE_SAVE_TICKS :: 30 // about a second at REFRESH_PERIOD of window size stability
MAX_COLUMNS :: 1024
MAX_CHANNELS :: 2
REFRESH_PERIOD :: 33 * 1_000_000
RULER_MIN_MAJOR_PX :: 90.0 // closest allowed spacing of labelled ticks
RULER_TICK_HEIGHT :: 10
SCROLLBAR_HEIGHT :: 12
LAYOUT_CHROME_WIDTH :: 40 // window padding plus panel padding, used before a surface is measured
TEXT :: alicorn.Color{0.09, 0.12, 0.17, 1}
TEXT_LIGHT :: alicorn.Color{0.93, 0.95, 0.98, 1}

CMD_UNDO   :: host.Application_Command_ID(1)
CMD_REDO   :: host.Application_Command_ID(2)
CMD_DELETE :: host.Application_Command_ID(3)
CMD_ZOOM_IN  :: host.Application_Command_ID(20)
CMD_ZOOM_OUT :: host.Application_Command_ID(21)
CMD_ZOOM_FIT :: host.Application_Command_ID(22)
CMD_ZOOM_SEL :: host.Application_Command_ID(23)
CMD_OPEN    :: host.Application_Command_ID(10)
CMD_SAVE    :: host.Application_Command_ID(11)
CMD_SAVE_AS :: host.Application_Command_ID(12)
CMD_EXPORT  :: host.Application_Command_ID(13)

DIALOG_OPEN    :: host.Dialog_ID(1)
DIALOG_SAVE_AS :: host.Dialog_ID(2)
DIALOG_EXPORT  :: host.Dialog_ID(3)

Channel_View :: struct {
	surface:  alicorn.Node_ID,
	width:    f32,
	height:   f32,
	columns:  i32,
	revision: u64,
	playhead: i32, // pixel column of the playhead, -1 when idle
	sel0:     i32, // pixel columns of the selection, equal when empty
	sel1:     i32,
	left:     f64, // view the geometry was built for
	pps:      f64,
	drawn:    bool,
}

Recoverable_Entry :: struct {
	modified: f64, // Unix seconds
	bytes:    f64,
}

Recovery_Action :: enum {
	None,
	Recover,
	Arm, // first click on Discard
	Discard,
}

App :: struct {
	backend:   Backend,
	// Unsaved work left by earlier sessions, listed by the backend at start-up.
	recoverables:   [dynamic]Recoverable_Entry,
	discard_armed:  int, // entry waiting for a second click, -1 for none
	pending_action: Recovery_Action, // applied on the next tick, never while building
	pending_index:  int,
	last_dir:       string, // directory of the last file dialog result
	window_height:  int,
	saved_width, saved_height: int,
	size_stable:    int,
	channels:  int,
	rate:      f64,
	duration:  f64,
	playing:   bool,
	position:  f64,
	status:    string,
	sel0, sel1: f64,
	selection_text: string,
	dragging:  bool,
	drag_anchor: f64,
	file_items: [5]host.Application_Menu_Item,
	edit_items: [4]host.Application_Menu_Item,
	menus:     [3]host.Application_Menu,
	dialogs:   host.Dialog_Service,
	dialog_open: bool, // one native dialog at a time
	project_path: string,
	unsaved:   bool,
	views:     [MAX_CHANNELS]Channel_View,
	scheduler: host.Application_Scheduler,
	// Horizontal view. The values live in Audacity's ViewInfo (zoom is clamped
	// there and saved with the project); these are the last values read.
	view_left: f64,
	pps:       f64,
	window_width: int,
	surface_x: f32,     // window x of the waveform surfaces
	surface_width: f32, // measured width of a waveform surface
	fit_pending: bool,  // zoom to fit once a surface has been measured
	ruler:     Channel_View,
	scrollbar: Channel_View,
	scrollbar_dragging: bool,
	scrollbar_grab: f32, // pointer offset inside the thumb while dragging
	view_items: [4]host.Application_Menu_Item,
	// The view the current description (ruler labels, readout) was built for.
	built_left, built_pps: f64,
	built_width: f32,
}

// ---- Horizontal view -------------------------------------------------------

effective_width :: proc(app: ^App) -> f32 {
	if app.surface_width > 0 { return app.surface_width }
	return max(f32(app.window_width - LAYOUT_CHROME_WIDTH), 1)
}

visible_span :: proc(app: ^App) -> f64 {
	if app.pps <= 0 { return 0 }
	return f64(effective_width(app)) / app.pps
}

// Scroll limits are a host decision: the start of the project at the left, and
// the end of the project at most one tenth of a window from the right edge.
// The zoom itself is clamped by Audacity (ZoomInfo::SetZoom).
set_view_clamped :: proc(app: ^App, left, pps: f64) {
	low, high: f64
	acetate_backend_zoom_limits(&low, &high)
	applied := acetate_backend_set_view(app.backend, left, clamp(pps, low, high))
	span := f64(effective_width(app)) / applied
	max_left := max(0, app.duration - span * 0.1)
	applied = acetate_backend_set_view(app.backend, clamp(left, 0, max_left), applied)
	acetate_backend_get_view(app.backend, &app.view_left, &app.pps)
	_ = applied
}

scroll_by_pixels :: proc(app: ^App, pixels: f64) {
	set_view_clamped(app, app.view_left + pixels / app.pps, app.pps)
}

zoom_by :: proc(app: ^App, factor: f64, anchor_px: f64) {
	anchor_time := app.view_left + anchor_px / app.pps
	new_pps := app.pps * factor
	set_view_clamped(app, anchor_time - anchor_px / new_pps, new_pps)
	// If Audacity clamped the zoom, keep the anchor stable at the applied value.
	if app.pps != new_pps { set_view_clamped(app, anchor_time - anchor_px / app.pps, app.pps) }
}

zoom_fit :: proc(app: ^App) {
	if app.duration <= 0 { return }
	set_view_clamped(app, 0, f64(effective_width(app)) / app.duration)
}

zoom_selection :: proc(app: ^App) {
	if app.sel1 <= app.sel0 { return }
	set_view_clamped(app, app.sel0, f64(effective_width(app)) / (app.sel1 - app.sel0))
}

time_to_px :: proc(app: ^App, time: f64) -> f64 { return (time - app.view_left) * app.pps }

// ---- Ruler -----------------------------------------------------------------

Ruler :: struct {
	step:         f64, // seconds between labelled (major) ticks
	subdivisions: int, // minor ticks per major tick
	first:        i64, // index of the first major tick at or after the left edge
	count:        int,
}

// Smallest 1-2-5 step whose labelled ticks are at least RULER_MIN_MAJOR_PX apart.
ruler_layout :: proc(app: ^App) -> Ruler {
	r := Ruler{step = 1, subdivisions = 5}
	if app.pps <= 0 { return r }
	found := false
	for exponent in -7 ..= 8 {
		base := math.pow(10.0, f64(exponent))
		for mantissa, index in ([3]f64{1, 2, 5}) {
			if mantissa * base * app.pps >= RULER_MIN_MAJOR_PX {
				r.step = mantissa * base
				r.subdivisions = 4 if index == 1 else 5
				found = true
				break
			}
		}
		if found { break }
	}
	r.first = i64(math.ceil(app.view_left / r.step))
	last := i64(math.floor((app.view_left + visible_span(app)) / r.step))
	r.count = max(int(last - r.first + 1), 0)
	return r
}

format_ruler_time :: proc(time, step: f64) -> string {
	if step >= 1 {
		total := i64(math.round(time))
		return fmt.tprintf("%d:%02d", total / 60, total % 60)
	}
	decimals := max(int(math.ceil(-math.log10(step) - 1e-9)), 1)
	minutes := i64(time / 60)
	return fmt.tprintf("%d:%0*.*f", minutes, 3 + decimals, decimals, time - f64(minutes) * 60)
}

// Starts or stops playback through Audacity's audio engine. Runs on the host's
// main thread; the audio thread never calls back into this program.
toggle_playback :: proc(app: ^App) {
	if app.playing {
		acetate_backend_stop(app.backend)
		app.playing = false
		app.status = "Stopped"
		return
	}
	if status := acetate_backend_play(app.backend, 0, app.duration); status != 0 {
		app.status = fmt.aprintf("Play failed (%d): %s", status, acetate_backend_last_error(app.backend))
		return
	}
	app.playing = true
	app.status = "Playing"
}

AUDACITY_FILTER := [1]host.Dialog_Filter{{name = "Audacity project", pattern = "aup3"}}
WAV_FILTER := [1]host.Dialog_Filter{{name = "WAV audio", pattern = "wav"}}

// Opens a native file dialog. The result arrives later through on_dialog; the
// host never runs a nested event loop for it.
show_dialog :: proc(app: ^App, id: host.Dialog_ID) {
	if app.dialog_open { return }
	request := host.File_Dialog_Request{id = id, initial_location = app.last_dir}
	switch id {
	case DIALOG_OPEN:
		request.kind = .Open_File
		request.title = "Open project"
		request.filters = AUDACITY_FILTER[:]
		request.accept_label = "Open"
	case DIALOG_SAVE_AS:
		request.kind = .Save_File
		request.title = "Save project"
		request.filters = AUDACITY_FILTER[:]
		request.accept_label = "Save"
	case DIALOG_EXPORT:
		request.kind = .Save_File
		request.title = "Export WAV"
		request.filters = WAV_FILTER[:]
		request.accept_label = "Export"
	}
	if host.ShowFileDialog(app.dialogs, request) {
		app.dialog_open = true
	} else {
		app.status = "Could not open a file dialog"
	}
}

with_extension :: proc(path, extension: string) -> string {
	if strings.has_suffix(strings.to_lower(path, context.temp_allocator), extension) { return path }
	return fmt.aprintf("%s%s", path, extension)
}

save_to :: proc(app: ^App, path: string) {
	target := with_extension(path, ".aup3")
	set_status_from_backend(app, fmt.aprintf("Saved %s", target), acetate_backend_save(app.backend, fmt.ctprintf("%s", target)))
}

dialog_app :: proc(state: rawptr, rt: ^alicorn.Runtime, result: ^host.File_Dialog_Result) {
	app := cast(^App)state
	app.dialog_open = false
	if result.status == .Error {
		app.status = fmt.aprintf("File dialog failed: %s", result.error)
	} else if result.status == .Accepted && len(result.paths) > 0 {
		path := result.paths[0]
		// The next dialog starts where this one ended; it is kept across runs.
		directory := filepath.dir(path)
		if directory != "" && directory != "." {
			app.last_dir = strings.clone(directory)
			acetate_backend_set_pref_string(app.backend, "LastDir", fmt.ctprintf("%s", directory))
		}
		delete(directory)
		switch result.id {
		case DIALOG_OPEN:
			// Read the result before building the message: it reports whether the
			// file held unsaved changes from a crash.
			status := acetate_backend_open(app.backend, fmt.ctprintf("%s", path))
			set_status_from_backend(app, opened_text(app, path), status)
		case DIALOG_SAVE_AS:
			save_to(app, path)
		case DIALOG_EXPORT:
			target := with_extension(path, ".wav")
			set_status_from_backend(app, fmt.aprintf("Exported %s", target), acetate_backend_export_wav(app.backend, fmt.ctprintf("%s", target)))
		}
	}
	sync_project(app, rt)
	alicorn.invalidate_root(rt, "file dialog finished")
}

// Re-reads project facts after anything that can replace or change the
// project (open, edit, undo, save) and invalidates when the description's
// inputs changed.
sync_project :: proc(app: ^App, rt: ^alicorn.Runtime) {
	channels := 0
	rate := 0.0
	if acetate_backend_track_count(app.backend) > 0 {
		channels = min(int(acetate_backend_channel_count(app.backend, 0)), MAX_CHANNELS)
		rate = acetate_backend_track_rate(app.backend, 0)
	}
	duration := acetate_backend_project_end(app.backend)
	path := string(acetate_backend_project_path(app.backend))
	unsaved := acetate_backend_has_unsaved_changes(app.backend) != 0
	if channels != app.channels || rate != app.rate || duration != app.duration ||
	   path != app.project_path || unsaved != app.unsaved {
		if channels != app.channels {
			// New surfaces will be created by the next description.
			app.views = {}
		}
		app.channels, app.rate, app.duration, app.unsaved = channels, rate, duration, unsaved
		if path != app.project_path { app.project_path = strings.clone(path) }
		if rt != nil { alicorn.invalidate_root(rt, "project facts changed") }
	}
}

opened_text :: proc(app: ^App, path: string) -> string {
	if acetate_backend_was_recovered(app.backend) != 0 {
		return fmt.aprintf("Opened %s · recovered unsaved changes from the last session; save to keep them", path)
	}
	return fmt.aprintf("Opened %s", path)
}

// ---- Recovery --------------------------------------------------------------

refresh_recoverables :: proc(app: ^App) {
	clear(&app.recoverables)
	count := int(acetate_backend_recoverable_count(app.backend))
	for index in 0 ..< count {
		entry: Recoverable_Entry
		if acetate_backend_recoverable_info(app.backend, i32(index), &entry.modified, &entry.bytes) == 0 {
			append(&app.recoverables, entry)
		}
	}
	app.discard_armed = -1
}

age_text :: proc(modified: f64) -> string {
	seconds := f64(time.time_to_unix(time.now())) - modified
	switch {
	case seconds < 90:    return "moments ago"
	case seconds < 5400:  return fmt.tprintf("%d min ago", int(math.round(seconds / 60)))
	case seconds < 129600: return fmt.tprintf("%d h ago", int(math.round(seconds / 3600)))
	}
	return fmt.tprintf("%d days ago", int(math.round(seconds / 86400)))
}

size_text :: proc(bytes: f64) -> string {
	if bytes >= 1048576 { return fmt.tprintf("%.1f MB", bytes / 1048576) }
	return fmt.tprintf("%.0f KB", max(bytes / 1024, 1))
}

// Applies a click made while the description was built. Loading or deleting a
// project is too heavy and too state-changing to do in the middle of a build.
apply_pending_recovery :: proc(app: ^App, rt: ^alicorn.Runtime) {
	action, index := app.pending_action, app.pending_index
	app.pending_action = .None
	if action == .None { return }
	switch action {
	case .Arm:
		app.discard_armed = index
		alicorn.invalidate_root(rt, "discard armed")
		return
	case .Recover:
		if status := acetate_backend_recover(app.backend, i32(index)); status == 0 {
			app.status = "Recovered unsaved work; save it to keep it"
			app.fit_pending = true
		} else {
			app.status = fmt.aprintf("Recovery failed: %s", acetate_backend_last_error(app.backend))
		}
	case .Discard:
		if acetate_backend_discard_recoverable(app.backend, i32(index)) == 0 {
			app.status = "Discarded the unsaved project"
		} else {
			app.status = fmt.aprintf("Discard failed: %s", acetate_backend_last_error(app.backend))
		}
	case .None:
	}
	refresh_recoverables(app)
	sync_project(app, rt)
	alicorn.invalidate_root(rt, "recovery")
}

// Offered only while the window holds no project, so recovering never replaces
// work that is open. Discarding takes two clicks because it deletes for good.
build_recovery :: proc(app: ^App, ctx: ^alicorn.UI) {
	if app.channels > 0 || len(app.recoverables) == 0 { return }
	if ui.panel(ctx, padding = 10, fill = ui.rgb(0x2b3347), label = "recovery") {
		ui.text(ctx, "Unsaved work from an earlier session can be recovered", size = 16, color = TEXT_LIGHT)
		shown := min(len(app.recoverables), MAX_RECOVERABLES_SHOWN)
		for index in 0 ..< shown {
			entry := app.recoverables[index]
			if ui.row(ctx, key = u64(2000 + index), gap = 10, align = .Center) {
				ui.text(ctx, fmt.tprintf("%s · %s", age_text(entry.modified), size_text(entry.bytes)), key = u64(2100 + index), color = TEXT_LIGHT)
				if ui.button(ctx, "Recover", key = u64(2200 + index), width = 96) {
					app.pending_action, app.pending_index = .Recover, index
				}
				armed := app.discard_armed == index
				if ui.button(ctx, "Confirm" if armed else "Discard", key = u64(2300 + index), width = 96) {
					app.pending_action, app.pending_index = .Discard if armed else .Arm, index
				}
			}
		}
		if len(app.recoverables) > shown {
			ui.text(ctx, fmt.tprintf("and %d more", len(app.recoverables) - shown), color = TEXT_LIGHT)
		}
	}
}

set_status_from_backend :: proc(app: ^App, ok_text: string, status: i32) {
	if status == 0 {
		app.status = ok_text
	} else {
		app.status = fmt.aprintf("%s", acetate_backend_last_error(app.backend))
	}
}

run_command :: proc(app: ^App, rt: ^alicorn.Runtime, command: host.Application_Command_ID) {
	switch command {
	case CMD_UNDO:
		set_status_from_backend(app, "Undone", acetate_backend_undo(app.backend))
	case CMD_REDO:
		set_status_from_backend(app, "Redone", acetate_backend_redo(app.backend))
	case CMD_DELETE:
		set_status_from_backend(app, "Deleted selection", acetate_backend_delete_selection(app.backend))
	case CMD_ZOOM_IN:
		zoom_by(app, 2, f64(effective_width(app)) / 2)
	case CMD_ZOOM_OUT:
		zoom_by(app, 0.5, f64(effective_width(app)) / 2)
	case CMD_ZOOM_FIT:
		zoom_fit(app)
	case CMD_ZOOM_SEL:
		zoom_selection(app)
	case CMD_OPEN:
		show_dialog(app, DIALOG_OPEN)
	case CMD_SAVE:
		if app.project_path == "" {
			show_dialog(app, DIALOG_SAVE_AS)
		} else {
			save_to(app, app.project_path)
		}
	case CMD_SAVE_AS:
		show_dialog(app, DIALOG_SAVE_AS)
	case CMD_EXPORT:
		show_dialog(app, DIALOG_EXPORT)
	}
	sync_project(app, rt)
	alicorn.invalidate_root(rt, "command")
}

menu_command_app :: proc(state: rawptr, rt: ^alicorn.Runtime, command: host.Application_Command_ID) {
	run_command(cast(^App)state, rt, command)
}

// Maps a window-space pointer x onto project time through the current view,
// clamped to the audio.
time_for_x :: proc(app: ^App, x: f32) -> f64 {
	if app.pps <= 0 { return 0 }
	return clamp(app.view_left + f64(x - app.surface_x) / app.pps, 0, max(app.duration, 0))
}

channel_surface :: proc(app: ^App, target: alicorn.Node_ID) -> bool {
	if target == 0 { return false }
	for view in app.views[:app.channels] {
		if view.surface == target { return true }
	}
	return false
}

// Horizontal scrollbar geometry in surface pixels: the thumb spans the visible
// part of max(project, visible end).
scrollbar_thumb :: proc(app: ^App) -> (x, width, total: f32) {
	surface_width := effective_width(app)
	span := visible_span(app)
	range := max(app.duration, app.view_left + span)
	if range <= 0 { return 0, surface_width, range_to_f32(range) }
	thumb := clamp(f32(span / range) * surface_width, 24, surface_width)
	position := f32(app.view_left / range) * surface_width
	return clamp(position, 0, surface_width - thumb), thumb, range_to_f32(range)
}

range_to_f32 :: proc(range: f64) -> f32 { return f32(range) }

scrollbar_pointer :: proc(app: ^App, event: alicorn.Pointer_Event) {
	thumb_x, thumb_w, total := scrollbar_thumb(app)
	surface_width := effective_width(app)
	local_x := event.x - app.surface_x
	#partial switch event.kind {
	case .Down:
		if event.button != alicorn.POINTER_BUTTON_PRIMARY { return }
		app.scrollbar_dragging = true
		if local_x >= thumb_x && local_x <= thumb_x + thumb_w {
			app.scrollbar_grab = local_x - thumb_x
		} else {
			// A click on the track centres the thumb there.
			app.scrollbar_grab = thumb_w / 2
		}
		fallthrough
	case .Move:
		if !app.scrollbar_dragging || total <= 0 { return }
		fraction := f64((local_x - app.scrollbar_grab) / surface_width)
		set_view_clamped(app, fraction * f64(total), app.pps)
	case .Up, .Cancel:
		app.scrollbar_dragging = false
	}
}

pointer_app :: proc(state: rawptr, rt: ^alicorn.Runtime, event: alicorn.Pointer_Event, target: alicorn.Node_ID) {
	app := cast(^App)state
	if app.scrollbar_dragging || (target != 0 && target == app.scrollbar.surface) {
		scrollbar_pointer(app, event)
		return
	}
	#partial switch event.kind {
	case .Down:
		if event.button != alicorn.POINTER_BUTTON_PRIMARY { return }
		if app.duration > 0 && channel_surface(app, target) {
			time := time_for_x(app, event.x)
			app.dragging = true
			app.drag_anchor = time
			acetate_backend_set_selection(app.backend, time, time)
		}
	case .Move:
		if app.dragging {
			// The captured surface keeps receiving moves outside its bounds.
			acetate_backend_set_selection(app.backend, app.drag_anchor, time_for_x(app, event.x))
		}
	case .Up, .Cancel:
		app.dragging = false
	}
}

// Wheel and trackpad: Ctrl/Cmd + vertical motion zooms about the pointer;
// horizontal motion (or Shift + vertical motion) scrolls.
scroll_app :: proc(state: rawptr, rt: ^alicorn.Runtime, event: alicorn.Scroll_Event) {
	app := cast(^App)state
	if app.duration <= 0 { return }
	if event.modifiers.control || event.modifiers.super {
		if event.delta_y != 0 {
			// One wheel tick is about 1.0; trackpads send small fractions.
			zoom_by(app, math.exp(f64(event.delta_y) * 0.05), f64(event.x - app.surface_x))
		}
	} else {
		delta := event.delta_x
		if event.modifiers.shift || delta == 0 && event.modifiers.shift { delta = event.delta_y }
		// Positive deltas move the content right, so the view moves left.
		if delta != 0 { scroll_by_pixels(app, -f64(delta) * 10) }
	}
}

key_app :: proc(state: rawptr, rt: ^alicorn.Runtime, key: alicorn.Application_Key) -> bool {
	app := cast(^App)state
	if key == .Toggle {
		toggle_playback(app)
		alicorn.invalidate_root(rt, "playback toggled from keyboard")
		return true
	}
	if key == .Escape {
		acetate_backend_set_selection(app.backend, 0, 0)
		return true
	}
	return false
}

// Time ruler: labels are ordinary text cells positioned by a leading spacer
// (a retained surface carries geometry only), ticks are a retained surface.
build_ruler :: proc(app: ^App, ctx: ^alicorn.UI, dpi_scale: f32) {
	if !ui.column(ctx, padding = 4, label = "ruler") { return }
	layout := ruler_layout(app)
	step_px := f32(layout.step * app.pps)
	if ui.row(ctx, height = 18, clip = true, label = "ruler-labels") {
		if layout.count > 0 {
			offset := f32(time_to_px(app, f64(layout.first) * layout.step)) + 3
			ui.text(ctx, "", key = u64(1_000_000), style = ui.layout_style(width = max(offset, 0), height = 14))
			for index in 0 ..< layout.count {
				time := f64(layout.first + i64(index)) * layout.step
				ui.text(ctx, format_ruler_time(time, layout.step), key = u64(index), size = 12, color = TEXT,
					style = ui.layout_style(width = step_px, height = 14))
			}
		}
	}
	app.ruler.surface = alicorn.gpu_geometry_surface(
		ctx,
		"time-ruler-ticks",
		ui.layout_style(width = -1, height = RULER_TICK_HEIGHT),
		dpi_scale,
	)
}

build_app :: proc(
	state: rawptr,
	rt: ^alicorn.Runtime,
	logical_width, logical_height: int,
	dpi_scale: f32,
) -> alicorn.Node_ID {
	app := cast(^App)state
	app.window_width = logical_width
	app.window_height = logical_height
	app.built_left, app.built_pps, app.built_width = app.view_left, app.pps, effective_width(app)
	ctx, should_build := alicorn.begin_frame(rt)
	if !should_build { return 0 }

	root := alicorn.container_begin(&ctx, .Root, label = "acetate-host-root", style = ui.layout_style(grow = 1))
	if ui.column(&ctx, grow = 1, padding = 16, gap = 8, clip = true, label = "acetate-host-content") {
		ui.text(&ctx, "Acetate · Alicorn host spike", size = 22, color = TEXT)
		ui.text(&ctx, fmt.tprintf(
			"%s%s · %d channel(s) · %.0f Hz · %.3f s",
			app.project_path if app.project_path != "" else "Unsaved project",
			" *" if app.unsaved else "",
			app.channels, app.rate, app.duration,
		), color = TEXT)
		if ui.row(&ctx, gap = 12, align = .Center, label = "transport") {
			if ui.button(&ctx, "Stop" if app.playing else "Play", width = 96) {
				toggle_playback(app)
			}
			ui.text(&ctx, app.status, color = TEXT)
			ui.text(&ctx, app.selection_text, color = TEXT)
		}
		build_recovery(app, &ctx)
		if app.channels > 0 { build_ruler(app, &ctx, dpi_scale) }
		for channel in 0 ..< app.channels {
			label := fmt.tprintf("channel-%d-panel", channel)
			if ui.panel(&ctx, key = u64(channel), grow = 1, padding = 4, clip = true, fill = ui.rgb(0x131822), label = label) {
				app.views[channel].surface = alicorn.gpu_geometry_surface(
					&ctx,
					fmt.tprintf("channel-%d-waveform", channel),
					ui.layout_style(grow = 1, width = -1, height = -1),
					dpi_scale,
					.Pointer,
				)
			}
		}
		if app.channels > 0 {
			if ui.panel(&ctx, padding = 4, fill = ui.rgb(0x131822), label = "scrollbar-panel") {
				app.scrollbar.surface = alicorn.gpu_geometry_surface(
					&ctx,
					"horizontal-scrollbar",
					ui.layout_style(width = -1, height = SCROLLBAR_HEIGHT),
					dpi_scale,
					.Pointer,
				)
			}
			ui.text(&ctx, fmt.tprintf(
				"View %.3f – %.3f s · %.1f px/s · wheel scrolls with Shift, zooms with Ctrl/Cmd",
				app.view_left, app.view_left + visible_span(app), app.pps,
			), color = TEXT)
		}
	}
	alicorn.container_end(&ctx)
	alicorn.end_frame(&ctx)
	return root
}

services_app :: proc(state: rawptr, services: host.Application_Services) {
	app := cast(^App)state
	app.scheduler = services.scheduler
	app.dialogs = services.dialogs
}

start_app :: proc(state: rawptr, _waker: host.Application_Waker) {
	app := cast(^App)state
	host.application_schedule_after(app.scheduler, .Frequent, REFRESH_PERIOD)
}

// Rebuilds one channel's retained geometry from backend min/max columns, only
// when the surface size or the project revision changed.
refresh_channel :: proc(app: ^App, rt: ^alicorn.Runtime, channel: int) {
	view := &app.views[channel]
	surface, ok := alicorn.gpu_surface_context(rt, view.surface)
	if !ok || surface.logical_bounds.w <= 0 || surface.logical_bounds.h <= 0 { return }
	width := surface.logical_bounds.w
	height := surface.logical_bounds.h
	revision := acetate_backend_revision(app.backend)
	columns := i32(min(int(width), MAX_COLUMNS))
	if columns < 1 { columns = 1 }
	playhead: i32 = -1
	if app.playing && app.position >= 0 {
		if x := time_to_px(app, app.position); x >= 0 && x <= f64(width) { playhead = i32(x) }
	}
	sel0, sel1: i32
	if app.sel1 > app.sel0 {
		sel0 = i32(clamp(time_to_px(app, app.sel0), 0, f64(width)))
		sel1 = i32(clamp(time_to_px(app, app.sel1), 0, f64(width)))
	}
	if view.drawn && view.width == width && view.height == height && view.columns == columns &&
	   view.revision == revision && view.playhead == playhead && view.sel0 == sel0 && view.sel1 == sel1 &&
	   view.left == app.view_left && view.pps == app.pps {
		return
	}

	min_max: [MAX_COLUMNS * 2]f32
	count: i32 = -1
	if app.duration > 0 && app.pps > 0 {
		count = acetate_backend_waveform(
			app.backend, 0, i32(channel), app.view_left, app.view_left + f64(width) / app.pps, columns, raw_data(min_max[:]),
		)
	}
	have_wave := count == columns

	rectangles: [MAX_COLUMNS + 3]alicorn.GPU_Surface_Filled_Rectangle
	used := 0
	centre := height * 0.5
	// Zero line.
	rectangles[used] = alicorn.GPU_Surface_Filled_Rectangle{
		bounds = alicorn.Rect{0, centre, width, 1},
		color  = alicorn.Color{0.22, 0.31, 0.43, 0.9},
	}
	used += 1
	column_width := width / f32(columns)
	wave := alicorn.Color{0.25, 0.86, 0.98, 1}
	for column in 0 ..< (int(columns) if have_wave else 0) {
		low := clamp(min_max[2 * column], -1, 1)
		high := clamp(min_max[2 * column + 1], -1, 1)
		top := centre - high * centre
		bottom := centre - low * centre
		span := bottom - top
		if span < 1 { span = 1 }
		rectangles[used] = alicorn.GPU_Surface_Filled_Rectangle{
			bounds = alicorn.Rect{f32(column) * column_width, top, max(column_width, 1), span},
			color  = wave,
		}
		used += 1
	}
	if sel1 > sel0 {
		rectangles[used] = alicorn.GPU_Surface_Filled_Rectangle{
			bounds = alicorn.Rect{f32(sel0), 0, f32(sel1 - sel0), height},
			color  = alicorn.Color{1, 1, 1, 0.22},
		}
		used += 1
	}
	if playhead >= 0 {
		rectangles[used] = alicorn.GPU_Surface_Filled_Rectangle{
			bounds = alicorn.Rect{f32(playhead), 0, 2, height},
			color  = alicorn.Color{1, 0.74, 0.28, 1},
		}
		used += 1
	}
	if alicorn.gpu_surface_update_geometry_with_rectangles(rt, view.surface, rectangles[:used], nil, nil) {
		view.width, view.height, view.columns, view.revision, view.playhead, view.sel0, view.sel1 = width, height, columns, revision, playhead, sel0, sel1
		view.left, view.pps = app.view_left, app.pps
		view.drawn = true
	}
}

refresh_ruler :: proc(app: ^App, rt: ^alicorn.Runtime) {
	view := &app.ruler
	surface, ok := alicorn.gpu_surface_context(rt, view.surface)
	if !ok || surface.logical_bounds.w <= 0 || surface.logical_bounds.h <= 0 { return }
	width, height := surface.logical_bounds.w, surface.logical_bounds.h
	if view.drawn && view.width == width && view.height == height && view.left == app.view_left && view.pps == app.pps {
		return
	}
	rectangles: [256]alicorn.GPU_Surface_Filled_Rectangle
	used := 0
	layout := ruler_layout(app)
	ink := alicorn.Color{0.09, 0.12, 0.17, 1}
	minor_step := layout.step / f64(layout.subdivisions)
	first_minor := i64(math.ceil(app.view_left / minor_step))
	last_minor := i64(math.floor((app.view_left + f64(width) / app.pps) / minor_step))
	for index := first_minor; index <= last_minor && used < len(rectangles); index += 1 {
		x := f32(time_to_px(app, f64(index) * minor_step))
		major := index % i64(layout.subdivisions) == 0
		tick_height := height if major else height * 0.45
		rectangles[used] = alicorn.GPU_Surface_Filled_Rectangle{bounds = alicorn.Rect{x, height - tick_height, 1, tick_height}, color = ink}
		used += 1
	}
	if alicorn.gpu_surface_update_geometry_with_rectangles(rt, view.surface, rectangles[:used], nil, nil) {
		view.width, view.height, view.left, view.pps, view.drawn = width, height, app.view_left, app.pps, true
	}
}

refresh_scrollbar :: proc(app: ^App, rt: ^alicorn.Runtime) {
	view := &app.scrollbar
	surface, ok := alicorn.gpu_surface_context(rt, view.surface)
	if !ok || surface.logical_bounds.w <= 0 || surface.logical_bounds.h <= 0 { return }
	width, height := surface.logical_bounds.w, surface.logical_bounds.h
	if view.drawn && view.width == width && view.height == height && view.left == app.view_left &&
	   view.pps == app.pps && view.revision == u64(app.duration * 1000) {
		return
	}
	thumb_x, thumb_w, _ := scrollbar_thumb(app)
	rectangles := [2]alicorn.GPU_Surface_Filled_Rectangle{
		{bounds = alicorn.Rect{0, 0, width, height}, color = alicorn.Color{0.13, 0.17, 0.24, 1}},
		{bounds = alicorn.Rect{thumb_x, 1, thumb_w, height - 2}, color = alicorn.Color{0.55, 0.62, 0.74, 1}},
	}
	if alicorn.gpu_surface_update_geometry_with_rectangles(rt, view.surface, rectangles[:], nil, nil) {
		view.width, view.height, view.left, view.pps, view.drawn = width, height, app.view_left, app.pps, true
		view.revision = u64(app.duration * 1000)
	}
}

selection_label :: proc(app: ^App) -> string {
	if app.sel1 > app.sel0 {
		return fmt.aprintf("Selection %.3f – %.3f s (%.3f s)", app.sel0, app.sel1, app.sel1 - app.sel0)
	}
	return "No selection · drag on a waveform"
}

// Native menu item state is read from this borrowed storage when a menu opens
// or an accelerator is processed, so it is refreshed on the application thread.
refresh_menu_state :: proc(app: ^App) {
	idle := !app.playing
	app.file_items[0].state.enabled = idle && !app.dialog_open
	app.file_items[1].state.enabled = idle && !app.dialog_open && app.channels > 0
	app.file_items[2].state.enabled = idle && !app.dialog_open && app.channels > 0
	app.file_items[4].state.enabled = idle && !app.dialog_open && app.duration > 0
	app.edit_items[0].state.enabled = idle && acetate_backend_can_undo(app.backend) != 0
	app.edit_items[1].state.enabled = idle && acetate_backend_can_redo(app.backend) != 0
	app.edit_items[3].state.enabled = idle && app.sel1 > app.sel0
	app.view_items[0].state.enabled = app.duration > 0
	app.view_items[1].state.enabled = app.duration > 0
	app.view_items[2].state.enabled = app.duration > 0
	app.view_items[3].state.enabled = app.sel1 > app.sel0
}

// Remembers the window size once it has stopped changing, so dragging a window
// edge does not rewrite the preferences file on every tick.
save_window_size :: proc(app: ^App) {
	width, height := app.window_width, app.window_height
	if width <= 0 || height <= 0 || (width == app.saved_width && height == app.saved_height) {
		app.size_stable = 0
		return
	}
	app.size_stable += 1
	if app.size_stable < SIZE_SAVE_TICKS { return }
	acetate_backend_set_pref_int(app.backend, "WindowWidth", c.long(width))
	acetate_backend_set_pref_int(app.backend, "WindowHeight", c.long(height))
	app.saved_width, app.saved_height, app.size_stable = width, height, 0
}

scheduled_wake_app :: proc(state: rawptr, rt: ^alicorn.Runtime, _class: host.Scheduled_Wake_Class) {
	app := cast(^App)state
	acetate_backend_update(app.backend)
	apply_pending_recovery(app, rt)
	save_window_size(app)
	if app.playing && acetate_backend_is_playing(app.backend) == 0 {
		// The stream reached its end by itself.
		app.playing = false
		app.status = "Finished"
		alicorn.invalidate_root(rt, "playback finished")
	}
	app.position = acetate_backend_play_position(app.backend)
	sync_project(app, rt)
	acetate_backend_get_selection(app.backend, &app.sel0, &app.sel1)

	// Measure the waveform surface; the ruler labels depend on its width.
	if app.channels > 0 {
		if surface, ok := alicorn.gpu_surface_context(rt, app.views[0].surface); ok && surface.logical_bounds.w > 0 {
			app.surface_width = surface.logical_bounds.w
			app.surface_x = surface.logical_bounds.x
		}
	}
	acetate_backend_get_view(app.backend, &app.view_left, &app.pps)
	if app.fit_pending && app.surface_width > 0 && app.duration > 0 {
		zoom_fit(app)
		app.fit_pending = false
	}
	// Follow the playhead: page the view when playback leaves it.
	if app.playing && app.position >= 0 && app.pps > 0 {
		if app.position < app.view_left || app.position > app.view_left + visible_span(app) {
			set_view_clamped(app, app.position, app.pps)
		}
	}
	if app.view_left != app.built_left || app.pps != app.built_pps || effective_width(app) != app.built_width {
		alicorn.invalidate_root(rt, "view changed")
	}
	refresh_menu_state(app)
	text := selection_label(app)
	if text != app.selection_text {
		app.selection_text = text
		alicorn.invalidate_root(rt, "selection changed")
	}
	for channel in 0 ..< app.channels {
		refresh_channel(app, rt, channel)
	}
	refresh_ruler(app, rt)
	refresh_scrollbar(app, rt)
	host.application_schedule_after(app.scheduler, .Frequent, REFRESH_PERIOD)
}

main :: proc() {
	smoke := false
	path := ""
	for argument, index in os.args {
		if index == 0 { continue }
		if argument == "--smoke" { smoke = true } else { path = argument }
	}
	if acetate_backend_abi_version() != EXPECTED_ABI {
		fmt.eprintln("backend ABI mismatch")
		os.exit(1)
	}
	app := App{status = "Ready · Space plays, drag to select", position = -1, pps = 86.1328125}
	app.backend = acetate_backend_create()
	if app.backend == nil {
		fmt.eprintln("could not create the Audacity backend")
		os.exit(1)
	}
	defer acetate_backend_destroy(app.backend)
	app.discard_armed = -1
	app.last_dir = strings.clone(string(acetate_backend_pref_string(app.backend, "LastDir")))
	window_width := clamp(int(acetate_backend_pref_int(app.backend, "WindowWidth", DEFAULT_WINDOW_WIDTH)), 480, 8192)
	window_height := clamp(int(acetate_backend_pref_int(app.backend, "WindowHeight", DEFAULT_WINDOW_HEIGHT)), 320, 8192)
	app.saved_width, app.saved_height = window_width, window_height
	refresh_recoverables(&app)
	if path == "" {
		app.status = "Open a project (Cmd/Ctrl+O)"
		if len(app.recoverables) > 0 { app.status = "Unsaved work from an earlier session can be recovered" }
	} else {
		cpath := fmt.ctprintf("%s", path)
		if strings.has_suffix(strings.to_lower(path, context.temp_allocator), ".aup3") {
			if status := acetate_backend_open(app.backend, cpath); status != 0 {
				fmt.eprintfln("open failed (%d): %s", status, acetate_backend_last_error(app.backend))
				os.exit(1)
			}
			if acetate_backend_was_recovered(app.backend) != 0 {
				app.status = "Recovered unsaved changes from the last session; save to keep them"
			}
		} else if status := acetate_backend_import(app.backend, cpath); status != 0 {
			fmt.eprintfln("import failed (%d): %s", status, acetate_backend_last_error(app.backend))
			os.exit(1)
		} else {
			// A fresh import starts fitted to the window; a saved project keeps its stored view.
			app.fit_pending = true
		}
	}
	sync_project(&app, nil)
	if path != "" {
		fmt.printfln("loaded %s: %d channel(s), %.0f Hz, %.3f s", path, app.channels, app.rate, app.duration)
	}

	app.edit_items = {
		{kind = .Command, command = CMD_UNDO, label = "Undo", state = {enabled = false}, shortcut = {key = 'z', modifiers = {.Primary}}},
		{kind = .Command, command = CMD_REDO, label = "Redo", state = {enabled = false}, shortcut = {key = 'z', modifiers = {.Primary, .Shift}}},
		{kind = .Separator},
		{kind = .Command, command = CMD_DELETE, label = "Delete", state = {enabled = false}, shortcut = {key = rune(8)}},
	}
	app.file_items = {
		{kind = .Command, command = CMD_OPEN, label = "Open…", state = {enabled = true}, shortcut = {key = 'o', modifiers = {.Primary}}},
		{kind = .Command, command = CMD_SAVE, label = "Save", state = {enabled = false}, shortcut = {key = 's', modifiers = {.Primary}}},
		{kind = .Command, command = CMD_SAVE_AS, label = "Save As…", state = {enabled = false}, shortcut = {key = 's', modifiers = {.Primary, .Shift}}},
		{kind = .Separator},
		{kind = .Command, command = CMD_EXPORT, label = "Export WAV…", state = {enabled = false}, shortcut = {key = 'e', modifiers = {.Primary, .Shift}}},
	}
	app.view_items = {
		{kind = .Command, command = CMD_ZOOM_IN, label = "Zoom In", state = {enabled = false}, shortcut = {key = '=', modifiers = {.Primary}}},
		{kind = .Command, command = CMD_ZOOM_OUT, label = "Zoom Out", state = {enabled = false}, shortcut = {key = '-', modifiers = {.Primary}}},
		{kind = .Command, command = CMD_ZOOM_FIT, label = "Zoom to Fit", state = {enabled = false}, shortcut = {key = '0', modifiers = {.Primary}}},
		{kind = .Command, command = CMD_ZOOM_SEL, label = "Zoom to Selection", state = {enabled = false}, shortcut = {key = 'e', modifiers = {.Primary}}},
	}
	app.menus = {
		{label = "File", items = app.file_items[:]},
		{label = "Edit", items = app.edit_items[:]},
		{label = "View", items = app.view_items[:]},
	}

	host.Run(host.Application{
		state = rawptr(&app),
		title = "Acetate (Alicorn host spike)",
		width = window_width,
		height = window_height,
		build = build_app,
		menus = app.menus[:],
		on_menu_command = menu_command_app,
		on_dialog = dialog_app,
		on_pointer = pointer_app,
		on_scroll = scroll_app,
		on_key = key_app,
		on_services = services_app,
		on_start = start_app,
		on_scheduled_wake = scheduled_wake_app,
	}, smoke = smoke)
}
