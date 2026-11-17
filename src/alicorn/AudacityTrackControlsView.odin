package audacity_alicorn_snap_view

import alicorn "alicorn:runtime"
import odin_runtime "base:runtime"
import "core:mem"

// Presentation-only runtime for track-control panels (title bar, buttons,
// sliders). It paints retained geometry plus text; Audacity's track-panel cells
// keep all hit testing, state and policy. Coordinates are track-panel local.

AUDACITY_TRACK_CONTROLS_OUTPUT_GEOMETRY :: u32(0)
AUDACITY_TRACK_CONTROLS_OUTPUT_LABELS :: u32(1)

Track_Controls_View :: struct {
	runtime: alicorn.Runtime,
	geometry_output: alicorn.Presentation_Output_ID,
	labels_output: alicorn.Presentation_Output_ID,
	geometry_registered, labels_registered: bool,
	geometry_id: alicorn.Node_ID,
	label_text_ids: [MAX_TIMELINE_LABELS]alicorn.Node_ID,
	label_text_count: int,
	width, height: f32,
	runtime_initialized: bool,
	callback_active: bool,
}

valid_track_controls_view :: proc(view: ^Track_Controls_View) -> bool {
	return view != nil && view.runtime_initialized && !view.callback_active
}

track_controls_output_id :: proc(
	view: ^Track_Controls_View,
	output_kind: u32,
) -> (id: alicorn.Presentation_Output_ID, ok: bool) {
	if view == nil { return }
	switch output_kind {
	case AUDACITY_TRACK_CONTROLS_OUTPUT_GEOMETRY:
		return view.geometry_output, true
	case AUDACITY_TRACK_CONTROLS_OUTPUT_LABELS:
		return view.labels_output, true
	}
	return
}

track_controls_build_description :: proc(
	view: ^Track_Controls_View,
	labels: []AudacityTimelineLabel,
) -> bool {
	ui, should_build := alicorn.begin_frame(&view.runtime)
	if !should_build { return false }

	_ = alicorn.container_begin(
		&ui,
		.Root,
		key=alicorn.key_string("audacity-alicorn-track-controls-root"),
		style=alicorn.layout_style(.Column, width=view.width, height=view.height, align=.Stretch),
	)
	columns := [1]alicorn.Grid_Track{alicorn.grid_fixed(view.width)}
	rows := [1]alicorn.Grid_Track{alicorn.grid_fixed(view.height)}
	grid := alicorn.grid_begin(
		&ui,
		alicorn.key_string("audacity-alicorn-track-controls-grid"),
		columns[:],
		rows[:],
		style=alicorn.layout_style(.Row, width=view.width, height=view.height, align=.Stretch),
	)

	geometry_region := alicorn.container_begin(
		&ui,
		.Container,
		key=alicorn.key_string("audacity-alicorn-track-controls-geometry-region"),
		style=alicorn.layout_style(.Row, width=view.width, height=view.height, align=.Stretch),
	)
	geometry_bound := geometry_region != 0 &&
		alicorn.presentation_output_bind_root(&view.runtime, view.geometry_output, geometry_region) == .Ok
	geometry_columns := [1]alicorn.Grid_Track{alicorn.grid_fixed(view.width)}
	geometry_rows := [1]alicorn.Grid_Track{alicorn.grid_fixed(view.height)}
	geometry_grid := alicorn.grid_begin(
		&ui,
		alicorn.key_string("audacity-alicorn-track-controls-geometry-grid"),
		geometry_columns[:],
		geometry_rows[:],
		style=alicorn.layout_style(.Row, width=view.width, height=view.height, align=.Stretch),
	)
	view.geometry_id = alicorn.gpu_geometry_surface(
		&ui,
		"audacity-alicorn-track-controls-geometry",
		alicorn.layout_style(.Row, width=view.width, height=view.height),
		1,
	)
	geometry_ok := view.geometry_id != 0 && alicorn.grid_cell(&ui, view.geometry_id, 0, 0)
	alicorn.grid_end(&ui)
	alicorn.container_end(&ui)
	geometry_region_ok := geometry_region != 0 && alicorn.grid_cell(&ui, geometry_region, 0, 0)

	labels_region := alicorn.container_begin(
		&ui,
		.Container,
		key=alicorn.key_string("audacity-alicorn-track-controls-labels-region"),
		style=alicorn.layout_style(.Row, width=view.width, height=view.height, align=.Start, clip=true),
	)
	labels_bound := labels_region != 0 &&
		alicorn.presentation_output_bind_root(&view.runtime, view.labels_output, labels_region) == .Ok
	labels_columns := [1]alicorn.Grid_Track{alicorn.grid_fixed(view.width)}
	labels_rows := [1]alicorn.Grid_Track{alicorn.grid_fixed(view.height)}
	labels_grid := alicorn.grid_begin(
		&ui,
		alicorn.key_string("audacity-alicorn-track-controls-labels-grid"),
		labels_columns[:],
		labels_rows[:],
		style=alicorn.layout_style(.Row, width=view.width, height=view.height, align=.Stretch),
	)
	view.label_text_count = 0
	labels_ok := true
	for label, index in labels {
		if label.width <= 0 || label.height <= 0 || label.text_length == 0 { continue }
		// Each label is a full-size column placed in the same grid cell, so
		// labels overlay each other and are positioned by spacers.
		label_root := alicorn.container_begin(
			&ui,
			.Container,
			key=alicorn.key_u64(u64(index+1)),
			style=alicorn.layout_style(.Column, width=view.width, height=view.height, align=.Start),
		)
		top := min(f32(max(label.y, 0)), view.height)
		if top > 0 {
			_ = alicorn.container_begin(
				&ui,
				.Container,
				key=alicorn.key_u64(1),
				style=alicorn.layout_style(.Row, width=view.width, height=top),
			)
			alicorn.container_end(&ui)
		}
		row_height := min(f32(label.height), max(view.height-top, 0.0))
		_ = alicorn.container_begin(
			&ui,
			.Container,
			key=alicorn.key_u64(2),
			style=alicorn.layout_style(.Row, width=view.width, height=row_height, align=.Start),
		)
		left := min(f32(max(label.x, 0)), view.width)
		if left > 0 {
			_ = alicorn.container_begin(
				&ui,
				.Container,
				key=alicorn.key_u64(1),
				style=alicorn.layout_style(.Row, width=left, height=row_height),
			)
			alicorn.container_end(&ui)
		}
		text_style := alicorn.Text_Style{
			font_weight=label.font_weight,
			font_size=label.font_size,
			overflow=.Clip,
		}
		text_value := string(mem.slice_ptr(label.text_utf8, int(label.text_length)))
		text_id := alicorn.text(
			&ui,
			text_value,
			key=alicorn.key_u64(2),
			style=alicorn.layout_style(width=f32(label.width)+4, height=row_height),
			text_style=text_style,
			color=alicorn.Color{label.color.r, label.color.g, label.color.b, label.color.a},
		)
		alicorn.container_end(&ui)
		alicorn.container_end(&ui)
		if label_root == 0 || !alicorn.grid_cell(&ui, label_root, 0, 0) { labels_ok = false }
		if text_id != 0 && view.label_text_count < MAX_TIMELINE_LABELS {
			view.label_text_ids[view.label_text_count] = text_id
			view.label_text_count += 1
		}
	}
	alicorn.grid_end(&ui)
	alicorn.container_end(&ui)
	labels_region_ok := labels_region != 0 && alicorn.grid_cell(&ui, labels_region, 0, 0)

	alicorn.grid_end(&ui)
	alicorn.container_end(&ui)
	alicorn.end_frame(&ui)
	return grid != 0 && geometry_bound && geometry_grid != 0 && geometry_ok && geometry_region_ok &&
		labels_bound && labels_grid != 0 && labels_ok && labels_region_ok
}

@(export, link_name="audacity_alicorn_track_controls_view_create")
audacity_alicorn_track_controls_view_create :: proc "c" (width, height: f32) -> rawptr {
	context = odin_runtime.default_context()
	if !alicorn.number_is_finite(width) || !alicorn.number_is_finite(height) || width <= 0 || height <= 0 {
		return nil
	}
	view := new(Track_Controls_View)
	view.width = width
	view.height = height
	view.runtime = alicorn.new_runtime(alicorn.Rect{0, 0, width, height})
	view.runtime_initialized = true
	fail :: proc(view: ^Track_Controls_View) -> rawptr {
		if view.geometry_registered {
			_ = alicorn.presentation_output_unregister(&view.runtime, view.geometry_output)
		}
		if view.labels_registered {
			_ = alicorn.presentation_output_unregister(&view.runtime, view.labels_output)
		}
		alicorn.destroy_runtime(&view.runtime)
		view.runtime_initialized = false
		free(view)
		return nil
	}
	if !alicorn.text_engine_load_default_fonts(&view.runtime) { return fail(view) }
	geometry_output, geometry_status := alicorn.presentation_output_register(&view.runtime)
	if geometry_status != .Ok { return fail(view) }
	view.geometry_output = geometry_output
	view.geometry_registered = true
	labels_output, labels_status := alicorn.presentation_output_register(&view.runtime)
	if labels_status != .Ok { return fail(view) }
	view.labels_output = labels_output
	view.labels_registered = true
	if !track_controls_build_description(view, nil) { return fail(view) }
	return rawptr(view)
}

@(export, link_name="audacity_alicorn_track_controls_view_destroy")
audacity_alicorn_track_controls_view_destroy :: proc "c" (opaque_view: rawptr) {
	if opaque_view == nil { return }
	context = odin_runtime.default_context()
	view := transmute(^Track_Controls_View)opaque_view
	if view.callback_active { return }
	if view.runtime_initialized {
		alicorn.destroy_runtime(&view.runtime)
		view.runtime_initialized = false
	}
	free(view)
}

@(export, link_name="audacity_alicorn_track_controls_view_resize")
audacity_alicorn_track_controls_view_resize :: proc "c" (
	opaque_view: rawptr,
	width, height: f32,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Track_Controls_View)opaque_view
	if !valid_track_controls_view(view) ||
	   !alicorn.number_is_finite(width) || !alicorn.number_is_finite(height) ||
	   width <= 0 || height <= 0 {
		return -1
	}
	if view.width == width && view.height == height { return 0 }
	view.width = width
	view.height = height
	_ = alicorn.runtime_set_viewport(&view.runtime, alicorn.Rect{0, 0, width, height})
	alicorn.invalidate_root(&view.runtime, "Audacity track controls resized")
	if !track_controls_build_description(view, nil) { return -1 }
	return 1
}

// Replaces the retained scene (geometry and labels) in one call.
@(export, link_name="audacity_alicorn_track_controls_view_update")
audacity_alicorn_track_controls_view_update :: proc "c" (
	opaque_view: rawptr,
	rectangle_input: ^AudacityWaveformRectangle,
	rectangle_count: u64,
	segment_input: ^AudacityWaveformSegment,
	segment_count: u64,
	point_marker_input: ^AudacityWaveformPointMarker,
	point_marker_count: u64,
	label_input: ^AudacityTimelineLabel,
	label_count: u64,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Track_Controls_View)opaque_view
	if !valid_track_controls_view(view) ||
	   (rectangle_count > 0 && rectangle_input == nil) ||
	   (segment_count > 0 && segment_input == nil) ||
	   (point_marker_count > 0 && point_marker_input == nil) ||
	   (label_count > 0 && label_input == nil) ||
	   rectangle_count > u64(MAX_TIMELINE_GEOMETRY_RECTANGLES) ||
	   segment_count > u64(MAX_TIMELINE_GEOMETRY_SEGMENTS) ||
	   point_marker_count > u64(MAX_TIMELINE_GEOMETRY_SEGMENTS) ||
	   label_count > u64(MAX_TIMELINE_LABELS) {
		return -1
	}
	remaining := u64(MAX_TIMELINE_GEOMETRY_SEGMENTS) - rectangle_count
	if segment_count > remaining { return -1 }
	remaining -= segment_count
	if point_marker_count > remaining { return -1 }

	labels := mem.slice_ptr(label_input, int(label_count))
	for label in labels {
		if label.text_length > MAX_TIMELINE_LABEL_BYTES ||
		   (label.text_length > 0 && label.text_utf8 == nil) ||
		   label.width < 0 || label.height < 0 ||
		   !alicorn.number_is_finite(label.font_size) || !alicorn.number_is_finite(label.font_weight) ||
		   label.font_size < 0 || label.font_size > 512 || label.font_weight < 1 || label.font_weight > 1000 ||
		   !timeline_color_valid(label.color) {
			return -1
		}
	}

	rectangle_slice := mem.slice_ptr(rectangle_input, int(rectangle_count))
	segment_slice := mem.slice_ptr(segment_input, int(segment_count))
	marker_slice := mem.slice_ptr(point_marker_input, int(point_marker_count))
	rectangles: [MAX_TIMELINE_GEOMETRY_RECTANGLES]alicorn.GPU_Surface_Filled_Rectangle
	for source, index in rectangle_slice {
		rectangles[index] = alicorn.GPU_Surface_Filled_Rectangle{
			bounds=alicorn.Rect{source.x, source.y, source.width, source.height},
			color=alicorn.Color{source.r, source.g, source.b, source.a},
		}
	}
	segments: [MAX_TIMELINE_GEOMETRY_SEGMENTS]alicorn.GPU_Surface_Line_Segment
	for source, index in segment_slice {
		segments[index] = alicorn.GPU_Surface_Line_Segment{
			start=alicorn.GPU_Surface_Point{source.x0, source.y0},
			end=alicorn.GPU_Surface_Point{source.x1, source.y1},
			thickness=source.thickness,
			color=alicorn.Color{source.r, source.g, source.b, source.a},
		}
	}
	point_markers: [MAX_TIMELINE_GEOMETRY_SEGMENTS]alicorn.GPU_Surface_Point_Marker
	for source, index in marker_slice {
		point_markers[index] = alicorn.GPU_Surface_Point_Marker{
			center=alicorn.GPU_Surface_Point{source.center_x, source.center_y},
			diameter=source.diameter,
			color=alicorn.Color{source.r, source.g, source.b, source.a},
		}
	}

	alicorn.invalidate_root(&view.runtime, "Audacity track controls changed")
	if !track_controls_build_description(view, labels) { return -1 }
	if !alicorn.gpu_surface_update_geometry_with_rectangles_and_markers(
		&view.runtime,
		view.geometry_id,
		rectangles[:int(rectangle_count)],
		segments[:int(segment_count)],
		nil,
		point_markers[:int(point_marker_count)],
	) {
		return -1
	}
	presentation, ready := alicorn.begin_presentation_frame(&view.runtime)
	if !ready { return -1 }
	alicorn.end_presentation_frame(&presentation)
	return 1
}

@(export, link_name="audacity_alicorn_track_controls_view_paint_geometry")
audacity_alicorn_track_controls_view_paint_geometry :: proc "c" (
	opaque_view: rawptr,
	callback: AudacityWaveformPaintCallback,
	callback_context: rawptr,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Track_Controls_View)opaque_view
	if !valid_track_controls_view(view) || callback == nil { return -1 }
	display, ok := alicorn.paint_display_list(&view.runtime)
	if !ok { return -1 }

	view.callback_active = true
	delivered := i32(0)
	for command in display.commands {
		if command.owner != view.geometry_id { continue }
		geometry, geometry_ok := command.payload.(alicorn.Geometry_Paint)
		if !geometry_ok { continue }
		payload, payload_ok := alicorn.gpu_surface_payload_resolve(&view.runtime, geometry.geometry)
		if !payload_ok || (len(payload.rectangles) == 0 && len(payload.segments) == 0 && len(payload.point_markers) == 0) { continue }

		output_rectangles: [MAX_TIMELINE_GEOMETRY_RECTANGLES]AudacityWaveformRectangle
		for rectangle, i in payload.rectangles {
			output_rectangles[i] = AudacityWaveformRectangle{
				x=rectangle.bounds.x, y=rectangle.bounds.y,
				width=rectangle.bounds.w, height=rectangle.bounds.h,
				r=rectangle.color.r, g=rectangle.color.g, b=rectangle.color.b, a=rectangle.color.a,
			}
		}
		output_segments: [MAX_TIMELINE_GEOMETRY_SEGMENTS]AudacityWaveformSegment
		for segment, i in payload.segments {
			output_segments[i] = AudacityWaveformSegment{
				x0=segment.start.x, y0=segment.start.y, x1=segment.end.x, y1=segment.end.y,
				thickness=segment.thickness,
				r=segment.color.r, g=segment.color.g, b=segment.color.b, a=segment.color.a,
			}
		}
		output_markers: [MAX_TIMELINE_GEOMETRY_SEGMENTS]AudacityWaveformPointMarker
		for marker, i in payload.point_markers {
			output_markers[i] = AudacityWaveformPointMarker{
				center_x=marker.center.x, center_y=marker.center.y, diameter=marker.diameter,
				r=marker.color.r, g=marker.color.g, b=marker.color.b, a=marker.color.a,
			}
		}
		rectangle_pointer: ^AudacityWaveformRectangle = nil
		if len(payload.rectangles) > 0 { rectangle_pointer = &output_rectangles[0] }
		segment_pointer: ^AudacityWaveformSegment = nil
		if len(payload.segments) > 0 { segment_pointer = &output_segments[0] }
		marker_pointer: ^AudacityWaveformPointMarker = nil
		if len(payload.point_markers) > 0 { marker_pointer = &output_markers[0] }
		callback(
			callback_context,
			rectangle_pointer, u64(len(payload.rectangles)),
			segment_pointer, u64(len(payload.segments)),
			marker_pointer, u64(len(payload.point_markers)),
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

@(export, link_name="audacity_alicorn_track_controls_view_paint_labels")
audacity_alicorn_track_controls_view_paint_labels :: proc "c" (
	opaque_view: rawptr,
	callback: AudacitySnapDrawCallback,
	callback_context: rawptr,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Track_Controls_View)opaque_view
	if !valid_track_controls_view(view) || callback == nil { return -1 }
	display, ok := alicorn.paint_display_list(&view.runtime)
	if !ok { return -1 }

	view.callback_active = true
	delivered := i32(0)
	for command in display.commands {
		payload, is_text := command.payload.(alicorn.Text_Paint)
		if !is_text { continue }
		is_label := false
		for i in 0..<view.label_text_count {
			if command.owner == view.label_text_ids[i] {
				is_label = true
				break
			}
		}
		if !is_label { continue }
		run, run_ok := alicorn.paint_text_run_resolve(&view.runtime, payload.run)
		if !run_ok { continue }
		output := snap_view_command_base(command, AUDACITY_SNAP_DRAW_TEXT)
		output.text_color = snap_color(payload.color)
		output.font_size = run.size
		output.text_length = u64(len(run.value))
		if len(run.value) > 0 { output.text_utf8 = raw_data(run.value) }
		callback(callback_context, &output)
		delivered += 1
	}
	view.callback_active = false
	return delivered
}

@(export, link_name="audacity_alicorn_track_controls_view_output_revision")
audacity_alicorn_track_controls_view_output_revision :: proc "c" (
	opaque_view: rawptr,
	output_kind: u32,
	revision: ^u64,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Track_Controls_View)opaque_view
	if !valid_track_controls_view(view) || revision == nil { return -1 }
	output, valid_output := track_controls_output_id(view, output_kind)
	if !valid_output { return -1 }
	snapshot, status := alicorn.presentation_output_snapshot(&view.runtime, output)
	if status != .Ok { return -1 }
	if !snapshot.pending { return 0 }
	revision^ = snapshot.revision
	return 1
}

@(export, link_name="audacity_alicorn_track_controls_view_output_submission_succeeded")
audacity_alicorn_track_controls_view_output_submission_succeeded :: proc "c" (
	opaque_view: rawptr,
	output_kind: u32,
	revision: u64,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Track_Controls_View)opaque_view
	if !valid_track_controls_view(view) { return -1 }
	output, valid_output := track_controls_output_id(view, output_kind)
	if !valid_output { return -1 }
	status := alicorn.presentation_output_frame_submission_succeeded(&view.runtime, output, revision)
	if status == .Ok { return 1 }
	return -1
}
