package audacity_alicorn_snap_view

import alicorn "alicorn:runtime"
import odin_runtime "base:runtime"
import "core:mem"

AudacityWaveformSegment :: struct {
	x0, y0, x1, y1: f32,
	thickness: f32,
	r, g, b, a: f32,
}

AudacityWaveformPaintCallback :: #type proc "c" (
	ctx: rawptr,
	segments: ^AudacityWaveformSegment,
	segment_count: u64,
	bounds: AudacitySnapRect,
	clip: AudacitySnapRect,
	translation_x, translation_y: f32,
)

Waveform_View :: struct {
	runtime: alicorn.Runtime,
	geometry_id: alicorn.Node_ID,
	runtime_initialized: bool,
	callback_active: bool,
}

MAX_WAVEFORM_GEOMETRY_SEGMENTS :: alicorn.GPU_SURFACE_MAX_VERTICES / 6

valid_waveform_view :: proc(view: ^Waveform_View) -> bool {
	return view != nil && view.runtime_initialized && !view.callback_active
}

waveform_view_build_description :: proc(view: ^Waveform_View) -> bool {
	ui, should_build := alicorn.begin_frame(&view.runtime)
	if !should_build { return false }

	viewport := alicorn.viewport_bounds(&view.runtime)
	root_style := alicorn.layout_style(
		.Column,
		width=viewport.w,
		height=viewport.h,
		align=.Stretch,
	)
	_ = alicorn.container_begin(
		&ui,
		.Root,
		key=alicorn.key_string("audacity-alicorn-waveform-root"),
		style=root_style,
	)
	view.geometry_id = alicorn.gpu_geometry_surface(
		&ui,
		"audacity-alicorn-waveform-geometry",
		alicorn.layout_style(.Row, width=viewport.w, height=viewport.h),
		1,
	)
	alicorn.container_end(&ui)
	alicorn.end_frame(&ui)
	return view.geometry_id != 0
}

@(export, link_name="audacity_alicorn_waveform_view_create")
audacity_alicorn_waveform_view_create :: proc "c" (width, height: f32) -> rawptr {
	context = odin_runtime.default_context()
	if !alicorn.number_is_finite(width) || !alicorn.number_is_finite(height) || width < 0 || height < 0 {
		return nil
	}
	view := new(Waveform_View)
	view.runtime = alicorn.new_runtime(alicorn.Rect{0, 0, width, height})
	view.runtime_initialized = true
	if !waveform_view_build_description(view) {
		alicorn.destroy_runtime(&view.runtime)
		view.runtime_initialized = false
		free(view)
		return nil
	}
	return rawptr(view)
}

@(export, link_name="audacity_alicorn_waveform_view_destroy")
audacity_alicorn_waveform_view_destroy :: proc "c" (opaque_view: rawptr) {
	if opaque_view == nil { return }
	context = odin_runtime.default_context()
	view := transmute(^Waveform_View)opaque_view
	if view.callback_active { return }
	if view.runtime_initialized {
		alicorn.destroy_runtime(&view.runtime)
		view.runtime_initialized = false
	}
	free(view)
}

@(export, link_name="audacity_alicorn_waveform_view_resize")
audacity_alicorn_waveform_view_resize :: proc "c" (
	opaque_view: rawptr,
	width, height: f32,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Waveform_View)opaque_view
	if !valid_waveform_view(view) || !alicorn.number_is_finite(width) ||
	   !alicorn.number_is_finite(height) || width < 0 || height < 0 {
		return -1
	}
	previous := alicorn.viewport_bounds(&view.runtime)
	if previous.x == 0 && previous.y == 0 && previous.w == width && previous.h == height {
		return 0
	}
	if !alicorn.runtime_set_viewport(&view.runtime, alicorn.Rect{0, 0, width, height}) {
		return -1
	}
	if !waveform_view_build_description(view) {
		return -1
	}
	return 1
}

@(export, link_name="audacity_alicorn_waveform_view_update")
audacity_alicorn_waveform_view_update :: proc "c" (
	opaque_view: rawptr,
	input: ^AudacityWaveformSegment,
	segment_count: u64,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Waveform_View)opaque_view
	if !valid_waveform_view(view) || (segment_count > 0 && input == nil) ||
	   segment_count > u64(MAX_WAVEFORM_GEOMETRY_SEGMENTS) {
		return -1
	}

	count := int(segment_count)
	input_segments := mem.slice_ptr(input, count)
	segments: [MAX_WAVEFORM_GEOMETRY_SEGMENTS]alicorn.GPU_Surface_Line_Segment
	for i in 0..<count {
		source := input_segments[i]
		segments[i] = alicorn.GPU_Surface_Line_Segment{
			start=alicorn.GPU_Surface_Point{source.x0, source.y0},
			end=alicorn.GPU_Surface_Point{source.x1, source.y1},
			thickness=source.thickness,
			color=alicorn.Color{source.r, source.g, source.b, source.a},
		}
	}
	if !alicorn.gpu_surface_update_geometry(
		&view.runtime,
		view.geometry_id,
		segments[:count],
		nil,
	) {
		return -1
	}
	presentation, ready := alicorn.begin_presentation_frame(&view.runtime)
	if !ready { return -1 }
	alicorn.end_presentation_frame(&presentation)
	return 1
}

@(export, link_name="audacity_alicorn_waveform_view_paint")
audacity_alicorn_waveform_view_paint :: proc "c" (
	opaque_view: rawptr,
	callback: AudacityWaveformPaintCallback,
	callback_context: rawptr,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Waveform_View)opaque_view
	if !valid_waveform_view(view) || callback == nil { return -1 }
	display, ok := alicorn.paint_display_list(&view.runtime)
	if !ok { return -1 }

	view.callback_active = true
	delivered := i32(0)
	for command in display.commands {
		if command.owner != view.geometry_id { continue }
		geometry, geometry_ok := command.payload.(alicorn.Geometry_Paint)
		if !geometry_ok { continue }
		payload, payload_ok := alicorn.gpu_surface_payload_resolve(&view.runtime, geometry.geometry)
		if !payload_ok || len(payload.segments) == 0 { continue }

		output: [MAX_WAVEFORM_GEOMETRY_SEGMENTS]AudacityWaveformSegment
		for i in 0..<len(payload.segments) {
			segment := payload.segments[i]
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
		callback(
			callback_context,
			&output[0],
			u64(len(payload.segments)),
			snap_rect(command.bounds),
			snap_rect(command.clip),
			command.translation[0],
			command.translation[1],
		)
		delivered += 1
	}
	view.callback_active = false
	return delivered
}
