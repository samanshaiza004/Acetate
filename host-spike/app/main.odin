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
	acetate_backend_waveform :: proc(b: Backend, track, channel: i32, t0, t1: f64, columns: i32, min_max: [^]f32) -> i32 ---
}

EXPECTED_ABI :: 1
MAX_COLUMNS :: 1024
MAX_CHANNELS :: 2
REFRESH_PERIOD :: 33 * 1_000_000
TEXT :: alicorn.Color{0.09, 0.12, 0.17, 1}

Channel_View :: struct {
	surface:  alicorn.Node_ID,
	width:    f32,
	height:   f32,
	columns:  i32,
	revision: u64,
	drawn:    bool,
}

App :: struct {
	backend:   Backend,
	file_name: string,
	channels:  int,
	rate:      f64,
	duration:  f64,
	views:     [MAX_CHANNELS]Channel_View,
	scheduler: host.Application_Scheduler,
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
		for channel in 0 ..< app.channels {
			label := fmt.tprintf("channel-%d-panel", channel)
			if ui.panel(&ctx, key = u64(channel), grow = 1, padding = 4, clip = true, fill = ui.rgb(0x131822), label = label) {
				app.views[channel].surface = alicorn.gpu_geometry_surface(
					&ctx,
					fmt.tprintf("channel-%d-waveform", channel),
					ui.layout_style(grow = 1, width = -1, height = -1),
					dpi_scale,
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
	if view.drawn && view.width == width && view.height == height && view.columns == columns && view.revision == revision {
		return
	}

	min_max: [MAX_COLUMNS * 2]f32
	count := acetate_backend_waveform(app.backend, 0, i32(channel), 0, app.duration, columns, raw_data(min_max[:]))
	if count != columns { return }

	rectangles: [MAX_COLUMNS + 1]alicorn.GPU_Surface_Filled_Rectangle
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
	for column in 0 ..< int(columns) {
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
	if alicorn.gpu_surface_update_geometry_with_rectangles(rt, view.surface, rectangles[:used], nil, nil) {
		view.width, view.height, view.columns, view.revision = width, height, columns, revision
		view.drawn = true
	}
}

scheduled_wake_app :: proc(state: rawptr, rt: ^alicorn.Runtime, _class: host.Scheduled_Wake_Class) {
	app := cast(^App)state
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
	app := App{}
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

	host.Run(host.Application{
		state = rawptr(&app),
		title = "Acetate (Alicorn host spike)",
		width = 1100,
		height = 640,
		build = build_app,
		on_services = services_app,
		on_start = start_app,
		on_scheduled_wake = scheduled_wake_app,
	}, smoke = smoke)
}
