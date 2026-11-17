package main

// Experimental Alicorn-owned host window for Acetate. Audacity's C++ project
// and track code stays authoritative: this program only asks the backend shim
// (host-spike/backend) to import a file and to summarise the waveform for the
// visible columns, then draws that bounded data through a retained GPU
// surface. It does not copy sample buffers and it never runs audio or backend
// work off the main thread.
import "core:fmt"
import "core:os"
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
	acetate_backend_waveform :: proc(b: Backend, track, channel: i32, t0, t1: f64, columns: i32, min_max: [^]f32) -> i32 ---
}

EXPECTED_ABI :: 4
MAX_COLUMNS :: 1024
MAX_CHANNELS :: 2
REFRESH_PERIOD :: 33 * 1_000_000
TEXT :: alicorn.Color{0.09, 0.12, 0.17, 1}

CMD_UNDO   :: host.Application_Command_ID(1)
CMD_REDO   :: host.Application_Command_ID(2)
CMD_DELETE :: host.Application_Command_ID(3)

Channel_View :: struct {
	surface:  alicorn.Node_ID,
	width:    f32,
	height:   f32,
	columns:  i32,
	revision: u64,
	playhead: i32, // pixel column of the playhead, -1 when idle
	sel0:     i32, // pixel columns of the selection, equal when empty
	sel1:     i32,
	drawn:    bool,
}

App :: struct {
	backend:   Backend,
	file_name: string,
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
	edit_items: [4]host.Application_Menu_Item,
	menus:     [1]host.Application_Menu,
	views:     [MAX_CHANNELS]Channel_View,
	scheduler: host.Application_Scheduler,
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
	}
	app.duration = acetate_backend_project_end(app.backend)
	alicorn.invalidate_root(rt, "edit command")
}

menu_command_app :: proc(state: rawptr, rt: ^alicorn.Runtime, command: host.Application_Command_ID) {
	run_command(cast(^App)state, rt, command)
}

// Maps a window-space pointer x onto project time using the channel surface
// that received the event, clamped to the audio.
time_at :: proc(app: ^App, rt: ^alicorn.Runtime, target: alicorn.Node_ID, x: f32) -> (time: f64, ok: bool) {
	if app.duration <= 0 { return 0, false }
	for view in app.views[:app.channels] {
		if view.surface != target || target == 0 { continue }
		surface, found := alicorn.gpu_surface_context(rt, view.surface)
		if !found || surface.logical_bounds.w <= 0 { return 0, false }
		fraction := f64((x - surface.logical_bounds.x) / surface.logical_bounds.w)
		return clamp(fraction, 0, 1) * app.duration, true
	}
	return 0, false
}

pointer_app :: proc(state: rawptr, rt: ^alicorn.Runtime, event: alicorn.Pointer_Event, target: alicorn.Node_ID) {
	app := cast(^App)state
	#partial switch event.kind {
	case .Down:
		if event.button != alicorn.POINTER_BUTTON_PRIMARY { return }
		if time, ok := time_at(app, rt, target, event.x); ok {
			app.dragging = true
			app.drag_anchor = time
			acetate_backend_set_selection(app.backend, time, time)
		}
	case .Move:
		if !app.dragging { return }
		// The captured surface keeps receiving moves outside its bounds.
		for view in app.views[:app.channels] {
			if time, ok := time_at(app, rt, view.surface, event.x); ok {
				acetate_backend_set_selection(app.backend, app.drag_anchor, time)
				break
			}
		}
	case .Up, .Cancel:
		app.dragging = false
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

build_app :: proc(
	state: rawptr,
	rt: ^alicorn.Runtime,
	_logical_width, _logical_height: int,
	dpi_scale: f32,
) -> alicorn.Node_ID {
	app := cast(^App)state
	ctx, should_build := alicorn.begin_frame(rt)
	if !should_build { return 0 }

	root := alicorn.container_begin(&ctx, .Root, label = "acetate-host-root", style = ui.layout_style(grow = 1))
	if ui.column(&ctx, grow = 1, padding = 16, gap = 8, clip = true, label = "acetate-host-content") {
		ui.text(&ctx, "Acetate · Alicorn host spike", size = 22, color = TEXT)
		ui.text(&ctx, fmt.tprintf(
			"%s · %d channel(s) · %.0f Hz · %.3f s",
			app.file_name, app.channels, app.rate, app.duration,
		), color = TEXT)
		if ui.row(&ctx, gap = 12, align = .Center, label = "transport") {
			if ui.button(&ctx, "Stop" if app.playing else "Play", width = 96) {
				toggle_playback(app)
			}
			ui.text(&ctx, app.status, color = TEXT)
			ui.text(&ctx, app.selection_text, color = TEXT)
		}
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
		ui.text(&ctx, fmt.tprintf("0.000 s  →  %.3f s", app.duration), color = TEXT)
	}
	alicorn.container_end(&ctx)
	alicorn.end_frame(&ctx)
	return root
}

services_app :: proc(state: rawptr, services: host.Application_Services) {
	app := cast(^App)state
	app.scheduler = services.scheduler
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
	if app.playing && app.position >= 0 && app.duration > 0 {
		playhead = i32(clamp(app.position / app.duration, 0, 1) * f64(width))
	}
	sel0, sel1: i32
	if app.duration > 0 {
		sel0 = i32(clamp(app.sel0 / app.duration, 0, 1) * f64(width))
		sel1 = i32(clamp(app.sel1 / app.duration, 0, 1) * f64(width))
	}
	if view.drawn && view.width == width && view.height == height && view.columns == columns &&
	   view.revision == revision && view.playhead == playhead && view.sel0 == sel0 && view.sel1 == sel1 {
		return
	}

	min_max: [MAX_COLUMNS * 2]f32
	count: i32 = -1
	if app.duration > 0 {
		count = acetate_backend_waveform(app.backend, 0, i32(channel), 0, app.duration, columns, raw_data(min_max[:]))
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
		view.drawn = true
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
	app.edit_items[0].state.enabled = idle && acetate_backend_can_undo(app.backend) != 0
	app.edit_items[1].state.enabled = idle && acetate_backend_can_redo(app.backend) != 0
	app.edit_items[3].state.enabled = idle && app.sel1 > app.sel0
}

scheduled_wake_app :: proc(state: rawptr, rt: ^alicorn.Runtime, _class: host.Scheduled_Wake_Class) {
	app := cast(^App)state
	acetate_backend_update(app.backend)
	if app.playing && acetate_backend_is_playing(app.backend) == 0 {
		// The stream reached its end by itself.
		app.playing = false
		app.status = "Finished"
		alicorn.invalidate_root(rt, "playback finished")
	}
	app.position = acetate_backend_play_position(app.backend)
	duration := acetate_backend_project_end(app.backend)
	if duration != app.duration {
		app.duration = duration
		alicorn.invalidate_root(rt, "project length changed")
	}
	acetate_backend_get_selection(app.backend, &app.sel0, &app.sel1)
	refresh_menu_state(app)
	text := selection_label(app)
	if text != app.selection_text {
		app.selection_text = text
		alicorn.invalidate_root(rt, "selection changed")
	}
	for channel in 0 ..< app.channels {
		refresh_channel(app, rt, channel)
	}
	host.application_schedule_after(app.scheduler, .Frequent, REFRESH_PERIOD)
}

main :: proc() {
	smoke := false
	path := ""
	for argument, index in os.args {
		if index == 0 { continue }
		if argument == "--smoke" { smoke = true } else { path = argument }
	}
	if path == "" {
		fmt.eprintln("usage: acetate-host [--smoke] <audio file>")
		os.exit(2)
	}
	if acetate_backend_abi_version() != EXPECTED_ABI {
		fmt.eprintln("backend ABI mismatch")
		os.exit(1)
	}
	app := App{status = "Ready · Space plays, drag to select", position = -1}
	app.backend = acetate_backend_create()
	if app.backend == nil {
		fmt.eprintln("could not create the Audacity backend")
		os.exit(1)
	}
	defer acetate_backend_destroy(app.backend)
	cpath := fmt.ctprintf("%s", path)
	if status := acetate_backend_import(app.backend, cpath); status != 0 {
		fmt.eprintfln("import failed (%d): %s", status, acetate_backend_last_error(app.backend))
		os.exit(1)
	}
	app.file_name = path
	app.channels = min(int(acetate_backend_channel_count(app.backend, 0)), MAX_CHANNELS)
	app.rate = acetate_backend_track_rate(app.backend, 0)
	app.duration = acetate_backend_project_end(app.backend)
	fmt.printfln("imported %s: %d channel(s), %.0f Hz, %.3f s", path, app.channels, app.rate, app.duration)

	app.edit_items = {
		{kind = .Command, command = CMD_UNDO, label = "Undo", state = {enabled = false}, shortcut = {key = 'z', modifiers = {.Primary}}},
		{kind = .Command, command = CMD_REDO, label = "Redo", state = {enabled = false}, shortcut = {key = 'z', modifiers = {.Primary, .Shift}}},
		{kind = .Separator},
		{kind = .Command, command = CMD_DELETE, label = "Delete", state = {enabled = false}, shortcut = {key = rune(8)}},
	}
	app.menus = {{label = "Edit", items = app.edit_items[:]}}

	host.Run(host.Application{
		state = rawptr(&app),
		title = "Acetate (Alicorn host spike)",
		width = 1100,
		height = 640,
		build = build_app,
		menus = app.menus[:],
		on_menu_command = menu_command_app,
		on_pointer = pointer_app,
		on_key = key_app,
		on_services = services_app,
		on_start = start_app,
		on_scheduled_wake = scheduled_wake_app,
	}, smoke = smoke)
}
