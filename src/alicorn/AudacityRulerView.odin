package audacity_alicorn_snap_view

import alicorn "alicorn:runtime"
import odin_runtime "base:runtime"
import "core:mem"

AudacityRulerTick :: struct {
	x, y0, y1, thickness: f32,
	color: AudacitySnapColor,
}

AudacityRulerLabel :: struct {
	x, y, width, height: i32,
	font_size, font_weight: f32,
	color: AudacitySnapColor,
	text_utf8: ^u8,
	text_length: u64,
}

AudacityRulerDrawCallback :: #type proc "c" (ctx: rawptr, command: ^AudacitySnapDrawCommand)

Ruler_View :: struct {
	runtime: alicorn.Runtime,
	geometry_id: alicorn.Node_ID,
	runtime_initialized: bool,
	callback_active: bool,
}

MAX_RULER_TICKS :: alicorn.GPU_SURFACE_MAX_VERTICES / 6
MAX_RULER_LABELS :: 1024
MAX_RULER_LABEL_BYTES :: 4096

valid_ruler_view :: proc(view: ^Ruler_View) -> bool {
	return view != nil && view.runtime_initialized && !view.callback_active
}

ruler_color_valid :: proc(color: AudacitySnapColor) -> bool {
	return alicorn.number_is_finite(color.r) && alicorn.number_is_finite(color.g) &&
		alicorn.number_is_finite(color.b) && alicorn.number_is_finite(color.a) &&
		color.r >= 0 && color.r <= 1 && color.g >= 0 && color.g <= 1 &&
		color.b >= 0 && color.b <= 1 && color.a >= 0 && color.a <= 1
}

ruler_view_build_description :: proc(view: ^Ruler_View, labels: []AudacityRulerLabel) -> bool {
	ui, should_build := alicorn.begin_frame(&view.runtime)
	if !should_build { return false }

	viewport := alicorn.viewport_bounds(&view.runtime)
	columns := [1]alicorn.Grid_Track{alicorn.grid_fixed(viewport.w)}
	rows := [1]alicorn.Grid_Track{alicorn.grid_fixed(viewport.h)}
	_ = alicorn.container_begin(
		&ui,
		.Root,
		key=alicorn.key_string("audacity-alicorn-ruler-root"),
		style=alicorn.layout_style(.Column, width=viewport.w, height=viewport.h, align=.Stretch),
	)
	grid := alicorn.grid_begin(
		&ui,
		alicorn.key_string("audacity-alicorn-ruler-grid"),
		columns[:],
		rows[:],
		style=alicorn.layout_style(.Row, width=viewport.w, height=viewport.h, align=.Stretch),
	)
	if grid == 0 {
		alicorn.container_end(&ui)
		alicorn.end_frame(&ui)
		return false
	}
	view.geometry_id = alicorn.gpu_geometry_surface(
		&ui,
		"audacity-alicorn-ruler-ticks",
		alicorn.layout_style(.Row, width=viewport.w, height=viewport.h),
		1,
	)
	grid_ok := view.geometry_id != 0 && alicorn.grid_cell(&ui, view.geometry_id, 0, 0)

	labels_row := alicorn.container_begin(
		&ui,
		.Container,
		key=alicorn.key_string("audacity-alicorn-ruler-labels"),
		style=alicorn.layout_style(.Row, width=viewport.w, height=viewport.h, align=.Start, clip=true),
	)
	cursor_x: i32 = 0
	for label, index in labels {
		if label.width <= 0 || label.height <= 0 || label.text_length == 0 { continue }
		label_x := max(label.x, cursor_x)
		if label_x > cursor_x {
			spacer := alicorn.container_begin(
				&ui,
				.Container,
				key=alicorn.key_u64(u64(index*2+1)),
				style=alicorn.layout_style(.Row, width=f32(label_x-cursor_x), height=viewport.h),
			)
			alicorn.container_end(&ui)
			_ = spacer
		}
		column := alicorn.container_begin(
			&ui,
			.Container,
			key=alicorn.key_u64(u64(index*2+2)),
			style=alicorn.layout_style(.Column, width=f32(label.width), height=viewport.h, align=.Start),
		)
		if label.y > 0 {
			_ = alicorn.container_begin(
				&ui,
				.Container,
				key=alicorn.key_u64(1),
				style=alicorn.layout_style(.Row, width=f32(label.width), height=min(f32(label.y), viewport.h)),
			)
			alicorn.container_end(&ui)
		}
		text_height := min(f32(label.height), max(viewport.h-f32(max(label.y, 0)), 0.0))
		text_style := alicorn.Text_Style{
			font_weight=label.font_weight,
			font_size=label.font_size,
			overflow=.Ellipsis,
		}
		text_value := string(mem.slice_ptr(label.text_utf8, int(label.text_length)))
		_ = alicorn.text(
			&ui,
			text_value,
			key=alicorn.key_u64(2),
			style=alicorn.layout_style(width=f32(label.width), height=text_height),
			text_style=text_style,
			color=alicorn.Color{label.color.r, label.color.g, label.color.b, label.color.a},
		)
		alicorn.container_end(&ui)
		cursor_x = label_x + label.width
	}
	alicorn.container_end(&ui)
	grid_ok = alicorn.grid_cell(&ui, labels_row, 0, 0) && grid_ok
	alicorn.grid_end(&ui)
	alicorn.container_end(&ui)
	alicorn.end_frame(&ui)
	return grid_ok
}

@(export, link_name="audacity_alicorn_ruler_view_create")
audacity_alicorn_ruler_view_create :: proc "c" (width, height: f32) -> rawptr {
	context = odin_runtime.default_context()
	if !alicorn.number_is_finite(width) || !alicorn.number_is_finite(height) || width <= 0 || height <= 0 {
		return nil
	}
	view := new(Ruler_View)
	view.runtime = alicorn.new_runtime(alicorn.Rect{0, 0, width, height})
	view.runtime_initialized = true
	if !ruler_view_build_description(view, nil) {
		alicorn.destroy_runtime(&view.runtime)
		free(view)
		return nil
	}
	return rawptr(view)
}

@(export, link_name="audacity_alicorn_ruler_view_destroy")
audacity_alicorn_ruler_view_destroy :: proc "c" (opaque_view: rawptr) {
	if opaque_view == nil { return }
	context = odin_runtime.default_context()
	view := transmute(^Ruler_View)opaque_view
	if view.callback_active { return }
	if view.runtime_initialized {
		alicorn.destroy_runtime(&view.runtime)
		view.runtime_initialized = false
	}
	free(view)
}

@(export, link_name="audacity_alicorn_ruler_view_resize")
audacity_alicorn_ruler_view_resize :: proc "c" (opaque_view: rawptr, width, height: f32) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Ruler_View)opaque_view
	if !valid_ruler_view(view) || !alicorn.number_is_finite(width) || !alicorn.number_is_finite(height) || width <= 0 || height <= 0 {
		return -1
	}
	previous := alicorn.viewport_bounds(&view.runtime)
	if previous.x == 0 && previous.y == 0 && previous.w == width && previous.h == height { return 0 }
	if !alicorn.runtime_set_viewport(&view.runtime, alicorn.Rect{0, 0, width, height}) { return -1 }
	return 1
}

@(export, link_name="audacity_alicorn_ruler_view_update")
audacity_alicorn_ruler_view_update :: proc "c" (
	opaque_view: rawptr,
	ticks: ^AudacityRulerTick,
	tick_count: u64,
	labels: ^AudacityRulerLabel,
	label_count: u64,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Ruler_View)opaque_view
	if !valid_ruler_view(view) || (tick_count > 0 && ticks == nil) ||
	   (label_count > 0 && labels == nil) || tick_count > u64(MAX_RULER_TICKS) ||
	   label_count > u64(MAX_RULER_LABELS) {
		return -1
	}
	tick_input := mem.slice_ptr(ticks, int(tick_count))
	label_input := mem.slice_ptr(labels, int(label_count))
	for tick in tick_input {
		if !alicorn.number_is_finite(tick.x) || !alicorn.number_is_finite(tick.y0) ||
		   !alicorn.number_is_finite(tick.y1) || !alicorn.number_is_finite(tick.thickness) ||
		   tick.thickness <= 0 || !ruler_color_valid(tick.color) {
			return -1
		}
	}
	for label in label_input {
		if label.text_length > MAX_RULER_LABEL_BYTES ||
		   (label.text_length > 0 && label.text_utf8 == nil) ||
		   label.width < 0 || label.height < 0 ||
		   !alicorn.number_is_finite(label.font_size) || !alicorn.number_is_finite(label.font_weight) ||
		   label.font_size < 0 || label.font_size > 512 || label.font_weight < 1 || label.font_weight > 1000 ||
		   !ruler_color_valid(label.color) {
			return -1
		}
	}

	alicorn.invalidate_root(&view.runtime, "Audacity ruler snapshot changed")
	if !ruler_view_build_description(view, label_input) { return -1 }
	geometry: [MAX_RULER_TICKS]alicorn.GPU_Surface_Line_Segment
	for tick, index in tick_input {
		geometry[index] = alicorn.GPU_Surface_Line_Segment{
			start=alicorn.GPU_Surface_Point{tick.x, tick.y0},
			end=alicorn.GPU_Surface_Point{tick.x, tick.y1},
			thickness=tick.thickness,
			color=alicorn.Color{tick.color.r, tick.color.g, tick.color.b, tick.color.a},
		}
	}
	if !alicorn.gpu_surface_update_geometry(&view.runtime, view.geometry_id, geometry[:int(tick_count)], nil) {
		return -1
	}
	presentation, ready := alicorn.begin_presentation_frame(&view.runtime)
	if !ready { return -1 }
	alicorn.end_presentation_frame(&presentation)
	return 1
}

@(export, link_name="audacity_alicorn_ruler_view_paint")
audacity_alicorn_ruler_view_paint :: proc "c" (
	opaque_view: rawptr,
	draw_callback: AudacityRulerDrawCallback,
	geometry_callback: AudacityWaveformPaintCallback,
	callback_context: rawptr,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Ruler_View)opaque_view
	if !valid_ruler_view(view) || draw_callback == nil || geometry_callback == nil { return -1 }
	display, ok := alicorn.paint_display_list(&view.runtime)
	if !ok { return -1 }
	view.callback_active = true
	delivered := i32(0)
	for command in display.commands {
		switch payload in command.payload {
		case alicorn.Text_Paint:
			run, run_ok := alicorn.paint_text_run_resolve(&view.runtime, payload.run)
			if !run_ok { continue }
			output := snap_view_command_base(command, AUDACITY_SNAP_DRAW_TEXT)
			output.text_color = snap_color(payload.color)
			output.text_length = u64(len(run.value))
			if len(run.value) > 0 { output.text_utf8 = raw_data(run.value) }
			draw_callback(callback_context, &output)
			delivered += 1
		case alicorn.Geometry_Paint:
			if command.owner != view.geometry_id { continue }
			geometry, geometry_ok := alicorn.gpu_surface_payload_resolve(&view.runtime, payload.geometry)
			if !geometry_ok || len(geometry.segments) == 0 { continue }
			output: [MAX_RULER_TICKS]AudacityWaveformSegment
			for i in 0..<len(geometry.segments) {
				segment := geometry.segments[i]
				output[i] = AudacityWaveformSegment{
					x0=segment.start.x,
					y0=segment.start.y,
					x1=segment.end.x,
					y1=segment.end.y,
					thickness=segment.thickness,
					r=segment.color.r,
					g=segment.color.g,
					b=segment.color.b,
					a=segment.color.a,
				}
			}
			geometry_callback(
				callback_context,
				&output[0],
				u64(len(geometry.segments)),
				snap_rect(command.bounds),
				snap_rect(command.clip),
				command.translation[0],
				command.translation[1],
			)
			delivered += 1
		case alicorn.Surface_Paint, alicorn.Backdrop_Effect_Paint, alicorn.Icon_Paint, alicorn.Image_Paint:
			continue
		case:
			continue
		}
	}
	view.callback_active = false
	return delivered
}
