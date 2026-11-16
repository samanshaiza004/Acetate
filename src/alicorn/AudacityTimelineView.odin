package audacity_alicorn_snap_view

import alicorn "alicorn:runtime"
import odin_runtime "base:runtime"
import "core:mem"

AudacityWaveformSegment :: struct {
	x0, y0, x1, y1: f32,
	thickness: f32,
	r, g, b, a: f32,
}

AudacityWaveformRectangle :: struct {
	x, y, width, height: f32,
	r, g, b, a: f32,
}

AudacityWaveformPointMarker :: struct {
	center_x, center_y, diameter: f32,
	r, g, b, a: f32,
}

AudacityWaveformSelection :: struct {
	phase: u32,
	anchor_x: f32,
	current_x: f32,
}

AudacityWaveformPaintCallback :: #type proc "c" (
	ctx: rawptr,
	rectangles: ^AudacityWaveformRectangle,
	rectangle_count: u64,
	segments: ^AudacityWaveformSegment,
	segment_count: u64,
	point_markers: ^AudacityWaveformPointMarker,
	point_marker_count: u64,
	bounds: AudacitySnapRect,
	clip: AudacitySnapRect,
	translation_x, translation_y: f32,
)

AudacityRulerTick :: struct {
	x, y0, y1, thickness: f32,
	color: AudacitySnapColor,
}

AudacityTimelineLabel :: struct {
	x, y, width, height: i32,
	font_size, font_weight: f32,
	color: AudacitySnapColor,
	text_utf8: ^u8,
	text_length: u64,
}

AudacityRulerDrawCallback :: #type proc "c" (
	ctx: rawptr,
	command: ^AudacitySnapDrawCommand,
)

Timeline_View :: struct {
	runtime: alicorn.Runtime,
	ruler_output: alicorn.Presentation_Output_ID,
	waveform_output: alicorn.Presentation_Output_ID,
	waveform_labels_output: alicorn.Presentation_Output_ID,
	waveform_label_text_ids: [MAX_TIMELINE_LABELS]alicorn.Node_ID,
	waveform_label_text_count: int,
	ruler_geometry_id: alicorn.Node_ID,
	ruler_overlay_geometry_id: alicorn.Node_ID,
	waveform_geometry_id: alicorn.Node_ID,
	clip_affordance_geometry_id: alicorn.Node_ID,
	waveform_caret_overlay_geometry_id: alicorn.Node_ID,
	waveform_playhead_overlay_geometry_id: alicorn.Node_ID,
	width, ruler_height, waveform_height: f32,
	runtime_initialized: bool,
	callback_active: bool,
	selection_drag_active: bool,
	selection_anchor_x, selection_current_x: f32,
	pan_drag_active: bool,
	pan_anchor_x, pan_current_x: f32,
	ruler_drag_active: bool,
	ruler_anchor_x, ruler_current_x: f32,
}

MAX_TIMELINE_GEOMETRY_SEGMENTS :: alicorn.GPU_SURFACE_MAX_VERTICES / 6
MAX_TIMELINE_GEOMETRY_RECTANGLES :: alicorn.GPU_SURFACE_MAX_VERTICES / alicorn.GPU_SURFACE_RECTANGLE_VERTICES
MAX_TIMELINE_LABELS :: 1024
MAX_TIMELINE_LABEL_BYTES :: 4096
AUDACITY_WAVEFORM_INDICATOR_CARET :: u32(0)
AUDACITY_WAVEFORM_INDICATOR_PLAYHEAD :: u32(1)
AUDACITY_TIMELINE_OUTPUT_RULER :: u32(0)
AUDACITY_TIMELINE_OUTPUT_WAVEFORM :: u32(1)
AUDACITY_TIMELINE_OUTPUT_WAVEFORM_LABELS :: u32(2)

valid_timeline_view :: proc(view: ^Timeline_View) -> bool {
	return view != nil && view.runtime_initialized && !view.callback_active
}

timeline_view_build_description :: proc(
	view: ^Timeline_View,
	ruler_labels: []AudacityTimelineLabel,
	waveform_labels: []AudacityTimelineLabel,
) -> bool {
	ui, should_build := alicorn.begin_frame(&view.runtime)
	if !should_build { return false }

	_ = alicorn.container_begin(
		&ui,
		.Root,
		key=alicorn.key_string("audacity-alicorn-timeline-root"),
		style=alicorn.layout_style(
			.Column,
			width=view.width,
			height=view.ruler_height+view.waveform_height,
			align=.Stretch,
		),
	)

	ruler_region := alicorn.container_begin(
		&ui,
		.Container,
		key=alicorn.key_string("audacity-alicorn-timeline-ruler-region"),
		style=alicorn.layout_style(
			.Column,
			width=view.width,
			height=view.ruler_height,
			align=.Stretch,
		),
	)
	ruler_output_bound := ruler_region != 0 &&
		alicorn.presentation_output_bind_root(&view.runtime, view.ruler_output, ruler_region) == .Ok
	columns := [1]alicorn.Grid_Track{alicorn.grid_fixed(view.width)}
	rows := [1]alicorn.Grid_Track{alicorn.grid_fixed(view.ruler_height)}
	ruler_grid := alicorn.grid_begin(
		&ui,
		alicorn.key_string("audacity-alicorn-timeline-ruler-grid"),
		columns[:],
		rows[:],
		style=alicorn.layout_style(.Row, width=view.width, height=view.ruler_height, align=.Stretch),
	)
	view.ruler_geometry_id = alicorn.gpu_geometry_surface(
		&ui,
		"audacity-alicorn-timeline-ruler-ticks",
		alicorn.layout_style(.Row, width=view.width, height=view.ruler_height),
		1,
		.Pointer,
	)
	ruler_geometry_ok := view.ruler_geometry_id != 0 &&
		alicorn.grid_cell(&ui, view.ruler_geometry_id, 0, 0)

	labels_row := alicorn.container_begin(
		&ui,
		.Container,
		key=alicorn.key_string("audacity-alicorn-timeline-ruler-labels"),
		style=alicorn.layout_style(
			.Row,
			width=view.width,
			height=view.ruler_height,
			align=.Start,
			clip=true,
		),
	)
	cursor_x: i32 = 0
	for label, index in ruler_labels {
		if label.width <= 0 || label.height <= 0 || label.text_length == 0 { continue }
		label_x := max(label.x, cursor_x)
		if label_x > cursor_x {
			_ = alicorn.container_begin(
				&ui,
				.Container,
				key=alicorn.key_u64(u64(index*2+1)),
				style=alicorn.layout_style(
					.Row,
					width=f32(label_x-cursor_x),
					height=view.ruler_height,
				),
			)
			alicorn.container_end(&ui)
		}
		_ = alicorn.container_begin(
			&ui,
			.Container,
			key=alicorn.key_u64(u64(index*2+2)),
			style=alicorn.layout_style(
				.Column,
				width=f32(label.width),
				height=view.ruler_height,
				align=.Start,
			),
		)
		if label.y > 0 {
			_ = alicorn.container_begin(
				&ui,
				.Container,
				key=alicorn.key_u64(1),
				style=alicorn.layout_style(
					.Row,
					width=f32(label.width),
					height=min(f32(label.y), view.ruler_height),
				),
			)
			alicorn.container_end(&ui)
		}
		text_height := min(
			f32(label.height),
			max(view.ruler_height-f32(max(label.y, 0)), 0.0),
		)
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
	ruler_labels_ok := alicorn.grid_cell(&ui, labels_row, 0, 0)
	view.ruler_overlay_geometry_id = alicorn.gpu_geometry_surface(
		&ui,
		"audacity-alicorn-timeline-ruler-overlay",
		alicorn.layout_style(.Row, width=view.width, height=view.ruler_height),
		1,
	)
	ruler_overlay_ok := view.ruler_overlay_geometry_id != 0 &&
		alicorn.grid_cell(&ui, view.ruler_overlay_geometry_id, 0, 0)
	alicorn.grid_end(&ui)
	alicorn.container_end(&ui)
	alicorn.container_end(&ui)

	waveform_region := alicorn.container_begin(
		&ui,
		.Container,
		key=alicorn.key_string("audacity-alicorn-timeline-waveform-region"),
		style=alicorn.layout_style(.Row, width=view.width, height=view.waveform_height, align=.Stretch),
	)
	waveform_region_columns := [1]alicorn.Grid_Track{alicorn.grid_fixed(view.width)}
	waveform_region_rows := [1]alicorn.Grid_Track{alicorn.grid_fixed(view.waveform_height)}
	waveform_region_grid := alicorn.grid_begin(
		&ui,
		alicorn.key_string("audacity-alicorn-timeline-waveform-region-grid"),
		waveform_region_columns[:],
		waveform_region_rows[:],
		style=alicorn.layout_style(.Row, width=view.width, height=view.waveform_height, align=.Stretch),
	)
	waveform_content_region := alicorn.container_begin(
		&ui,
		.Container,
		key=alicorn.key_string("audacity-alicorn-timeline-waveform-content"),
		style=alicorn.layout_style(.Row, width=view.width, height=view.waveform_height, align=.Stretch),
	)
	waveform_output_bound := waveform_content_region != 0 &&
		alicorn.presentation_output_bind_root(&view.runtime, view.waveform_output, waveform_content_region) == .Ok
	waveform_columns := [1]alicorn.Grid_Track{alicorn.grid_fixed(view.width)}
	waveform_rows := [1]alicorn.Grid_Track{alicorn.grid_fixed(view.waveform_height)}
	waveform_grid := alicorn.grid_begin(
		&ui,
		alicorn.key_string("audacity-alicorn-timeline-waveform-grid"),
		waveform_columns[:],
		waveform_rows[:],
		style=alicorn.layout_style(
			.Row,
			width=view.width,
			height=view.waveform_height,
			align=.Stretch,
		),
	)
	view.waveform_geometry_id = alicorn.gpu_geometry_surface(
		&ui,
		"audacity-alicorn-timeline-waveform",
		alicorn.layout_style(.Row, width=view.width, height=view.waveform_height),
		1,
		.Pointer,
	)
	waveform_geometry_ok := view.waveform_geometry_id != 0 &&
		alicorn.grid_cell(&ui, view.waveform_geometry_id, 0, 0)
	view.clip_affordance_geometry_id = alicorn.gpu_geometry_surface(
		&ui,
		"audacity-alicorn-timeline-clip-affordances",
		alicorn.layout_style(.Row, width=view.width, height=view.waveform_height),
		1,
	)
	clip_affordance_geometry_ok := view.clip_affordance_geometry_id != 0 &&
		alicorn.grid_cell(&ui, view.clip_affordance_geometry_id, 0, 0)
	view.waveform_caret_overlay_geometry_id = alicorn.gpu_geometry_surface(
		&ui,
		"audacity-alicorn-timeline-waveform-caret-overlay",
		alicorn.layout_style(.Row, width=view.width, height=view.waveform_height),
		1,
	)
	waveform_caret_overlay_ok := view.waveform_caret_overlay_geometry_id != 0 &&
		alicorn.grid_cell(&ui, view.waveform_caret_overlay_geometry_id, 0, 0)
	view.waveform_playhead_overlay_geometry_id = alicorn.gpu_geometry_surface(
		&ui,
		"audacity-alicorn-timeline-waveform-playhead-overlay",
		alicorn.layout_style(.Row, width=view.width, height=view.waveform_height),
		1,
	)
	waveform_playhead_overlay_ok := view.waveform_playhead_overlay_geometry_id != 0 &&
		alicorn.grid_cell(&ui, view.waveform_playhead_overlay_geometry_id, 0, 0)
	alicorn.grid_end(&ui)
	alicorn.container_end(&ui)
	waveform_content_ok := waveform_content_region != 0 &&
		alicorn.grid_cell(&ui, waveform_content_region, 0, 0)
	waveform_labels_region := alicorn.container_begin(
		&ui,
		.Container,
		key=alicorn.key_string("audacity-alicorn-timeline-waveform-labels"),
		style=alicorn.layout_style(.Row, width=view.width, height=view.waveform_height, align=.Start, clip=true),
	)
	waveform_labels_bound := waveform_labels_region != 0 &&
		alicorn.presentation_output_bind_root(&view.runtime, view.waveform_labels_output, waveform_labels_region) == .Ok
	view.waveform_label_text_count = 0
	waveform_label_cursor_x: i32 = 0
	for label, index in waveform_labels {
		if label.width <= 0 || label.height <= 0 || label.text_length == 0 { continue }
		label_x := max(label.x, waveform_label_cursor_x)
		if label_x > waveform_label_cursor_x {
			_ = alicorn.container_begin(
				&ui,
				.Container,
				key=alicorn.key_u64(u64(index*2+1)),
				style=alicorn.layout_style(.Row, width=f32(label_x-waveform_label_cursor_x), height=view.waveform_height),
			)
			alicorn.container_end(&ui)
		}
		_ = alicorn.container_begin(
			&ui,
			.Container,
			key=alicorn.key_u64(u64(index*2+2)),
			style=alicorn.layout_style(.Column, width=f32(label.width), height=view.waveform_height, align=.Start),
		)
		if label.y > 0 {
			_ = alicorn.container_begin(
				&ui,
				.Container,
				key=alicorn.key_u64(1),
				style=alicorn.layout_style(.Row, width=f32(label.width), height=min(f32(label.y), view.waveform_height)),
			)
			alicorn.container_end(&ui)
		}
		text_height := min(f32(label.height), max(view.waveform_height-f32(max(label.y, 0)), 0.0))
		text_style := alicorn.Text_Style{font_weight=label.font_weight, font_size=label.font_size, overflow=.Clip}
		text_value := string(mem.slice_ptr(label.text_utf8, int(label.text_length)))
		text_id := alicorn.text(
			&ui,
			text_value,
			key=alicorn.key_u64(2),
			style=alicorn.layout_style(width=f32(label.width), height=text_height),
			text_style=text_style,
			color=alicorn.Color{label.color.r, label.color.g, label.color.b, label.color.a},
		)
		if text_id != 0 && view.waveform_label_text_count < MAX_TIMELINE_LABELS {
			view.waveform_label_text_ids[view.waveform_label_text_count] = text_id
			view.waveform_label_text_count += 1
		}
		alicorn.container_end(&ui)
		waveform_label_cursor_x = label_x + label.width
	}
	alicorn.container_end(&ui)
	waveform_labels_region_ok := waveform_labels_region != 0 &&
		alicorn.grid_cell(&ui, waveform_labels_region, 0, 0)
	alicorn.grid_end(&ui)
	alicorn.container_end(&ui)
	alicorn.end_frame(&ui)
	return ruler_output_bound && waveform_output_bound && waveform_labels_bound && ruler_grid != 0 && ruler_geometry_ok && ruler_labels_ok && ruler_overlay_ok &&
		waveform_region_grid != 0 && waveform_grid != 0 && waveform_geometry_ok && clip_affordance_geometry_ok && waveform_caret_overlay_ok &&
		waveform_playhead_overlay_ok && waveform_content_ok && waveform_labels_region_ok
}

timeline_waveform_indicator_id :: proc(
	view: ^Timeline_View,
	indicator_kind: u32,
) -> alicorn.Node_ID {
	switch indicator_kind {
	case AUDACITY_WAVEFORM_INDICATOR_CARET:
		return view.waveform_caret_overlay_geometry_id
	case AUDACITY_WAVEFORM_INDICATOR_PLAYHEAD:
		return view.waveform_playhead_overlay_geometry_id
	}
	return 0
}

timeline_output_id :: proc(view: ^Timeline_View, output_kind: u32) -> (id: alicorn.Presentation_Output_ID, ok: bool) {
	if view == nil { return }
	switch output_kind {
	case AUDACITY_TIMELINE_OUTPUT_RULER:
		return view.ruler_output, true
	case AUDACITY_TIMELINE_OUTPUT_WAVEFORM:
		return view.waveform_output, true
	case AUDACITY_TIMELINE_OUTPUT_WAVEFORM_LABELS:
		return view.waveform_labels_output, true
	}
	return
}

@(export, link_name="audacity_alicorn_timeline_view_create")
audacity_alicorn_timeline_view_create :: proc "c" (
	ruler_width, ruler_height, waveform_width, waveform_height: f32,
) -> rawptr {
	context = odin_runtime.default_context()
	if !alicorn.number_is_finite(ruler_width) || !alicorn.number_is_finite(ruler_height) ||
	   !alicorn.number_is_finite(waveform_width) || !alicorn.number_is_finite(waveform_height) ||
	   ruler_width <= 0 || ruler_height <= 0 || waveform_width <= 0 || waveform_height <= 0 {
		return nil
	}
	view := new(Timeline_View)
	view.width = max(ruler_width, waveform_width)
	view.ruler_height = ruler_height
	view.waveform_height = waveform_height
	view.runtime = alicorn.new_runtime(alicorn.Rect{
		0,
		0,
		view.width,
		ruler_height+waveform_height,
	})
	view.runtime_initialized = true
	ruler_output, ruler_output_status := alicorn.presentation_output_register(&view.runtime)
	if ruler_output_status != .Ok {
		alicorn.destroy_runtime(&view.runtime)
		view.runtime_initialized = false
		free(view)
		return nil
	}
	waveform_output, waveform_output_status := alicorn.presentation_output_register(&view.runtime)
	if waveform_output_status != .Ok {
		_ = alicorn.presentation_output_unregister(&view.runtime, ruler_output)
		alicorn.destroy_runtime(&view.runtime)
		view.runtime_initialized = false
		free(view)
		return nil
	}
	waveform_labels_output, waveform_labels_status := alicorn.presentation_output_register(&view.runtime)
	if waveform_labels_status != .Ok {
		_ = alicorn.presentation_output_unregister(&view.runtime, waveform_output)
		_ = alicorn.presentation_output_unregister(&view.runtime, ruler_output)
		alicorn.destroy_runtime(&view.runtime)
		view.runtime_initialized = false
		free(view)
		return nil
	}
	view.ruler_output = ruler_output
	view.waveform_output = waveform_output
	view.waveform_labels_output = waveform_labels_output
	if !timeline_view_build_description(view, nil, nil) {
		_ = alicorn.presentation_output_unregister(&view.runtime, waveform_labels_output)
		_ = alicorn.presentation_output_unregister(&view.runtime, waveform_output)
		_ = alicorn.presentation_output_unregister(&view.runtime, ruler_output)
		alicorn.destroy_runtime(&view.runtime)
		view.runtime_initialized = false
		free(view)
		return nil
	}
	return rawptr(view)
}

@(export, link_name="audacity_alicorn_timeline_view_destroy")
audacity_alicorn_timeline_view_destroy :: proc "c" (opaque_view: rawptr) {
	if opaque_view == nil { return }
	context = odin_runtime.default_context()
	view := transmute(^Timeline_View)opaque_view
	if view.callback_active { return }
	if view.runtime_initialized {
		alicorn.destroy_runtime(&view.runtime)
		view.runtime_initialized = false
	}
	free(view)
}

@(export, link_name="audacity_alicorn_timeline_view_resize")
audacity_alicorn_timeline_view_resize :: proc "c" (
	opaque_view: rawptr,
	ruler_width, ruler_height, waveform_width, waveform_height: f32,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Timeline_View)opaque_view
	if !valid_timeline_view(view) ||
	   !alicorn.number_is_finite(ruler_width) || !alicorn.number_is_finite(ruler_height) ||
	   !alicorn.number_is_finite(waveform_width) || !alicorn.number_is_finite(waveform_height) ||
	   ruler_width <= 0 || ruler_height <= 0 || waveform_width <= 0 || waveform_height <= 0 {
		return -1
	}

	width := max(ruler_width, waveform_width)
	if view.width == width && view.ruler_height == ruler_height &&
	   view.waveform_height == waveform_height {
		return 0
	}
	view.width = width
	view.ruler_height = ruler_height
	view.waveform_height = waveform_height
	_ = alicorn.runtime_set_viewport(
		&view.runtime,
		alicorn.Rect{0, 0, width, ruler_height+waveform_height},
	)
	alicorn.invalidate_root(&view.runtime, "Audacity timeline regions resized")
	if !timeline_view_build_description(view, nil, nil) {
		return -1
	}
	return 1
}

timeline_view_pointer_callback :: proc(
	state: rawptr,
	rt: ^alicorn.Runtime,
	event: alicorn.Pointer_Event,
	target: alicorn.Node_ID,
) {
	view := transmute(^Timeline_View)state
	if view == nil || rt == nil { return }
	switch event.kind {
	case .Down:
		if event.button == alicorn.POINTER_BUTTON_PRIMARY {
			view.pan_drag_active = false
			view.selection_drag_active = target == view.waveform_geometry_id
			if !view.selection_drag_active { return }
			view.selection_anchor_x = event.x
			view.selection_current_x = event.x
		} else if event.button == alicorn.POINTER_BUTTON_MIDDLE {
			view.selection_drag_active = false
			view.pan_drag_active = target == view.waveform_geometry_id
			if !view.pan_drag_active { return }
			view.pan_anchor_x = event.x
			view.pan_current_x = event.x
		}
	case .Move:
		if view.selection_drag_active && event.button == alicorn.POINTER_BUTTON_PRIMARY {
			view.selection_current_x = event.x
		}
		if view.pan_drag_active && event.button == alicorn.POINTER_BUTTON_MIDDLE {
			view.pan_anchor_x = view.pan_current_x
			view.pan_current_x = event.x
		}
	case .Up:
		if view.selection_drag_active && event.button == alicorn.POINTER_BUTTON_PRIMARY {
			view.selection_current_x = event.x
			view.selection_drag_active = false
		}
		if view.pan_drag_active && event.button == alicorn.POINTER_BUTTON_MIDDLE {
			view.pan_anchor_x = view.pan_current_x
			view.pan_current_x = event.x
			view.pan_drag_active = false
		}
	case .Cancel:
		view.selection_drag_active = false
		view.pan_drag_active = false
	}
}

timeline_view_ruler_pointer_callback :: proc(
	state: rawptr,
	rt: ^alicorn.Runtime,
	event: alicorn.Pointer_Event,
	target: alicorn.Node_ID,
) {
	view := transmute(^Timeline_View)state
	if view == nil || rt == nil { return }
	switch event.kind {
	case .Down:
		if target != view.ruler_geometry_id || event.button != alicorn.POINTER_BUTTON_PRIMARY { return }
		view.ruler_drag_active = true
		view.ruler_anchor_x = event.x
		view.ruler_current_x = event.x
	case .Move:
		if view.ruler_drag_active { view.ruler_current_x = event.x }
	case .Up:
		if view.ruler_drag_active {
			view.ruler_current_x = event.x
			view.ruler_drag_active = false
		}
	case .Cancel:
		view.ruler_drag_active = false
	}
}

@(export, link_name="audacity_alicorn_timeline_view_pointer")
audacity_alicorn_timeline_view_pointer :: proc "c" (
	opaque_view: rawptr,
	kind: u32,
	x, y: f32,
	button: u32,
	selection: ^AudacityWaveformSelection,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Timeline_View)opaque_view
	if !valid_timeline_view(view) || selection == nil ||
	   !alicorn.number_is_finite(x) || !alicorn.number_is_finite(y) || button > 3 {
		return -1
	}
	pointer_kind: alicorn.Pointer_Kind
	switch kind {
	case 0: pointer_kind = .Move
	case 1: pointer_kind = .Down
	case 2: pointer_kind = .Up
	case 3: pointer_kind = .Cancel
	case: return -1
	}
	if (kind == 1 || kind == 2) &&
	   button != alicorn.POINTER_BUTTON_PRIMARY &&
	   button != alicorn.POINTER_BUTTON_MIDDLE {
		return 0
	}
	event := alicorn.Pointer_Event{
		kind=pointer_kind,
		x=x,
		y=y+view.ruler_height,
		button=int(button),
	}
	was_active := view.selection_drag_active
	if button == alicorn.POINTER_BUTTON_MIDDLE {
		was_active = view.pan_drag_active
	}
	target := alicorn.process_pointer(&view.runtime, event)
	_ = alicorn.application_dispatch_pointer(
		&view.runtime,
		rawptr(view),
		timeline_view_pointer_callback,
		event,
		target,
	)
	phase: u32 = 0
	active := view.selection_drag_active
	if button == alicorn.POINTER_BUTTON_MIDDLE {
		active = view.pan_drag_active
	}
	switch kind {
	case 1:
		if active { phase = 1 }
	case 0:
		if active { phase = 2 }
	case 2:
		if was_active { phase = 3 }
	case 3:
		if was_active { phase = 4 }
	}
	anchor_x := view.selection_anchor_x
	current_x := view.selection_current_x
	if view.pan_drag_active || button == alicorn.POINTER_BUTTON_MIDDLE {
		anchor_x = view.pan_anchor_x
		current_x = view.pan_current_x
	}
	selection^ = AudacityWaveformSelection{
		phase=phase,
		anchor_x=anchor_x,
		current_x=current_x,
	}
	return i32(phase)
}

@(export, link_name="audacity_alicorn_timeline_view_ruler_pointer")
audacity_alicorn_timeline_view_ruler_pointer :: proc "c" (
	opaque_view: rawptr,
	kind: u32,
	x, y: f32,
	button: u32,
	selection: ^AudacityWaveformSelection,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Timeline_View)opaque_view
	if !valid_timeline_view(view) || selection == nil ||
	   !alicorn.number_is_finite(x) || !alicorn.number_is_finite(y) || button > 3 {
		return -1
	}
	pointer_kind: alicorn.Pointer_Kind
	switch kind {
	case 0: pointer_kind = .Move
	case 1: pointer_kind = .Down
	case 2: pointer_kind = .Up
	case 3: pointer_kind = .Cancel
	case: return -1
	}
	if (kind == 1 || kind == 2) && button != alicorn.POINTER_BUTTON_PRIMARY { return 0 }
	event := alicorn.Pointer_Event{
		kind=pointer_kind,
		x=x,
		y=y,
		button=int(button),
	}
	was_active := view.ruler_drag_active
	target := alicorn.process_pointer(&view.runtime, event)
	_ = alicorn.application_dispatch_pointer(
		&view.runtime,
		rawptr(view),
		timeline_view_ruler_pointer_callback,
		event,
		target,
	)
	phase: u32 = 0
	switch kind {
	case 1:
		if view.ruler_drag_active { phase = 1 }
	case 0:
		if view.ruler_drag_active { phase = 2 }
	case 2:
		if was_active { phase = 3 }
	case 3:
		if was_active { phase = 4 }
	}
	selection^ = AudacityWaveformSelection{
		phase=phase,
		anchor_x=view.ruler_anchor_x,
		current_x=view.ruler_current_x,
	}
	return i32(phase)
}

@(export, link_name="audacity_alicorn_timeline_view_update_waveform")
audacity_alicorn_timeline_view_update_waveform :: proc "c" (
	opaque_view: rawptr,
	rectangle_input: ^AudacityWaveformRectangle,
	rectangle_count: u64,
	input: ^AudacityWaveformSegment,
	segment_count: u64,
	point_marker_input: ^AudacityWaveformPointMarker,
	point_marker_count: u64,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Timeline_View)opaque_view
	if !valid_timeline_view(view) ||
	   (rectangle_count > 0 && rectangle_input == nil) ||
	   (segment_count > 0 && input == nil) ||
	   (point_marker_count > 0 && point_marker_input == nil) ||
	   rectangle_count > u64(MAX_TIMELINE_GEOMETRY_RECTANGLES) ||
	   segment_count > u64(MAX_TIMELINE_GEOMETRY_SEGMENTS) ||
	   point_marker_count > u64(MAX_TIMELINE_GEOMETRY_SEGMENTS) {
		return -1
	}
	remaining_primitives := u64(MAX_TIMELINE_GEOMETRY_SEGMENTS) - rectangle_count
	if segment_count > remaining_primitives { return -1 }
	remaining_primitives -= segment_count
	if point_marker_count > remaining_primitives { return -1 }
	rectangle_input_slice := mem.slice_ptr(rectangle_input, int(rectangle_count))
	count := int(segment_count)
	input_segments := mem.slice_ptr(input, count)
	marker_count := int(point_marker_count)
	input_markers := mem.slice_ptr(point_marker_input, marker_count)
	rectangles: [MAX_TIMELINE_GEOMETRY_RECTANGLES]alicorn.GPU_Surface_Filled_Rectangle
	for source, index in rectangle_input_slice {
		rectangles[index] = alicorn.GPU_Surface_Filled_Rectangle{
			bounds=alicorn.Rect{source.x, source.y, source.width, source.height},
			color=alicorn.Color{source.r, source.g, source.b, source.a},
		}
	}
	segments: [MAX_TIMELINE_GEOMETRY_SEGMENTS]alicorn.GPU_Surface_Line_Segment
	for i in 0..<count {
		source := input_segments[i]
		segments[i] = alicorn.GPU_Surface_Line_Segment{
			start=alicorn.GPU_Surface_Point{source.x0, source.y0},
			end=alicorn.GPU_Surface_Point{source.x1, source.y1},
			thickness=source.thickness,
			color=alicorn.Color{source.r, source.g, source.b, source.a},
		}
	}
	point_markers: [MAX_TIMELINE_GEOMETRY_SEGMENTS]alicorn.GPU_Surface_Point_Marker
	for source, index in input_markers {
		point_markers[index] = alicorn.GPU_Surface_Point_Marker{
			center=alicorn.GPU_Surface_Point{source.center_x, source.center_y},
			diameter=source.diameter,
			color=alicorn.Color{source.r, source.g, source.b, source.a},
		}
	}
	if !alicorn.gpu_surface_update_geometry_with_rectangles_and_markers(
		&view.runtime,
		view.waveform_geometry_id,
		rectangles[:int(rectangle_count)],
		segments[:count],
		nil,
		point_markers[:marker_count],
	) {
		return -1
	}
	presentation, ready := alicorn.begin_presentation_frame(&view.runtime)
	if !ready { return -1 }
	alicorn.end_presentation_frame(&presentation)
	return 1
}

@(export, link_name="audacity_alicorn_timeline_view_update_clip_affordances")
audacity_alicorn_timeline_view_update_clip_affordances :: proc "c" (
	opaque_view: rawptr,
	rectangle_input: ^AudacityWaveformRectangle,
	rectangle_count: u64,
	point_marker_input: ^AudacityWaveformPointMarker,
	point_marker_count: u64,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Timeline_View)opaque_view
	if !valid_timeline_view(view) ||
	   (rectangle_count > 0 && rectangle_input == nil) ||
	   (point_marker_count > 0 && point_marker_input == nil) ||
	   rectangle_count > u64(MAX_TIMELINE_GEOMETRY_RECTANGLES) ||
	   point_marker_count > u64(MAX_TIMELINE_GEOMETRY_SEGMENTS) ||
	   rectangle_count+point_marker_count > u64(MAX_TIMELINE_GEOMETRY_SEGMENTS) {
		return -1
	}
	rectangle_input_slice := mem.slice_ptr(rectangle_input, int(rectangle_count))
	input_markers := mem.slice_ptr(point_marker_input, int(point_marker_count))
	rectangles: [MAX_TIMELINE_GEOMETRY_RECTANGLES]alicorn.GPU_Surface_Filled_Rectangle
	for source, index in rectangle_input_slice {
		if !alicorn.number_is_finite(source.x) || !alicorn.number_is_finite(source.y) ||
		   !alicorn.number_is_finite(source.width) || !alicorn.number_is_finite(source.height) ||
		   source.width < 0 || source.height < 0 ||
		   !timeline_color_valid(AudacitySnapColor{source.r, source.g, source.b, source.a}) {
			return -1
		}
		rectangles[index] = alicorn.GPU_Surface_Filled_Rectangle{
			bounds=alicorn.Rect{source.x, source.y, source.width, source.height},
			color=alicorn.Color{source.r, source.g, source.b, source.a},
		}
	}
	point_markers: [MAX_TIMELINE_GEOMETRY_SEGMENTS]alicorn.GPU_Surface_Point_Marker
	for source, index in input_markers {
		if !alicorn.number_is_finite(source.center_x) || !alicorn.number_is_finite(source.center_y) ||
		   !alicorn.number_is_finite(source.diameter) || source.diameter <= 0 ||
		   !timeline_color_valid(AudacitySnapColor{source.r, source.g, source.b, source.a}) {
			return -1
		}
		point_markers[index] = alicorn.GPU_Surface_Point_Marker{
			center=alicorn.GPU_Surface_Point{source.center_x, source.center_y},
			diameter=source.diameter,
			color=alicorn.Color{source.r, source.g, source.b, source.a},
		}
	}
	if !alicorn.gpu_surface_update_geometry_with_rectangles_and_markers(
		&view.runtime,
		view.clip_affordance_geometry_id,
		rectangles[:int(rectangle_count)],
		nil,
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

timeline_color_valid :: proc(color: AudacitySnapColor) -> bool {
	return alicorn.number_is_finite(color.r) && alicorn.number_is_finite(color.g) &&
		alicorn.number_is_finite(color.b) && alicorn.number_is_finite(color.a) &&
		color.r >= 0 && color.r <= 1 && color.g >= 0 && color.g <= 1 &&
		color.b >= 0 && color.b <= 1 && color.a >= 0 && color.a <= 1
}

@(export, link_name="audacity_alicorn_timeline_view_update_waveform_overlay")
audacity_alicorn_timeline_view_update_waveform_overlay :: proc "c" (
	opaque_view: rawptr,
	indicator_kind: u32,
	segment_input: ^AudacityWaveformSegment,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Timeline_View)opaque_view
	if !valid_timeline_view(view) { return -1 }
	indicator_id := timeline_waveform_indicator_id(view, indicator_kind)
	if indicator_id == 0 { return -1 }
	segments: [1]alicorn.GPU_Surface_Line_Segment
	segment_count := 0
	if segment_input != nil {
		source := segment_input^
		if !alicorn.number_is_finite(source.x0) || !alicorn.number_is_finite(source.y0) ||
		   !alicorn.number_is_finite(source.x1) || !alicorn.number_is_finite(source.y1) ||
		   !alicorn.number_is_finite(source.thickness) || source.thickness <= 0 ||
		   !timeline_color_valid(AudacitySnapColor{source.r, source.g, source.b, source.a}) {
			return -1
		}
		segments[0] = alicorn.GPU_Surface_Line_Segment{
			start=alicorn.GPU_Surface_Point{source.x0, source.y0},
			end=alicorn.GPU_Surface_Point{source.x1, source.y1},
			thickness=source.thickness,
			color=alicorn.Color{source.r, source.g, source.b, source.a},
		}
		segment_count = 1
	}
	if !alicorn.gpu_surface_update_geometry(
		&view.runtime,
		indicator_id,
		segments[:segment_count],
		nil,
	) {
		return -1
	}
	presentation, ready := alicorn.begin_presentation_frame(&view.runtime)
	if !ready { return -1 }
	alicorn.end_presentation_frame(&presentation)
	return 1
}

@(export, link_name="audacity_alicorn_timeline_view_update_description")
audacity_alicorn_timeline_view_update_description :: proc "c" (
	opaque_view: rawptr,
	ticks: ^AudacityRulerTick,
	tick_count: u64,
	ruler_labels: ^AudacityTimelineLabel,
	ruler_label_count: u64,
	waveform_labels: ^AudacityTimelineLabel,
	waveform_label_count: u64,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Timeline_View)opaque_view
	if !valid_timeline_view(view) || (tick_count > 0 && ticks == nil) ||
	   (ruler_label_count > 0 && ruler_labels == nil) ||
	   (waveform_label_count > 0 && waveform_labels == nil) ||
	   tick_count > u64(MAX_TIMELINE_GEOMETRY_SEGMENTS) ||
	   ruler_label_count > u64(MAX_TIMELINE_LABELS) ||
	   waveform_label_count > u64(MAX_TIMELINE_LABELS) {
		return -1
	}
	tick_input := mem.slice_ptr(ticks, int(tick_count))
	ruler_label_input := mem.slice_ptr(ruler_labels, int(ruler_label_count))
	waveform_label_input := mem.slice_ptr(waveform_labels, int(waveform_label_count))
	for tick in tick_input {
		if !alicorn.number_is_finite(tick.x) || !alicorn.number_is_finite(tick.y0) ||
		   !alicorn.number_is_finite(tick.y1) || !alicorn.number_is_finite(tick.thickness) ||
		   tick.thickness <= 0 || !timeline_color_valid(tick.color) {
			return -1
		}
	}
	for label in ruler_label_input {
		if label.text_length > MAX_TIMELINE_LABEL_BYTES ||
		   (label.text_length > 0 && label.text_utf8 == nil) ||
		   label.width < 0 || label.height < 0 ||
		   !alicorn.number_is_finite(label.font_size) || !alicorn.number_is_finite(label.font_weight) ||
		   label.font_size < 0 || label.font_size > 512 || label.font_weight < 1 || label.font_weight > 1000 ||
		   !timeline_color_valid(label.color) {
			return -1
		}
	}
	for label in waveform_label_input {
		if label.text_length > MAX_TIMELINE_LABEL_BYTES ||
		   (label.text_length > 0 && label.text_utf8 == nil) ||
		   label.width < 0 || label.height < 0 ||
		   !alicorn.number_is_finite(label.font_size) || !alicorn.number_is_finite(label.font_weight) ||
		   label.font_size < 0 || label.font_size > 512 || label.font_weight < 1 || label.font_weight > 1000 ||
		   !timeline_color_valid(label.color) {
			return -1
		}
	}

	alicorn.invalidate_root(&view.runtime, "Audacity timeline labels changed")
	if !timeline_view_build_description(view, ruler_label_input, waveform_label_input) { return -1 }
	geometry: [MAX_TIMELINE_GEOMETRY_SEGMENTS]alicorn.GPU_Surface_Line_Segment
	for tick, index in tick_input {
		geometry[index] = alicorn.GPU_Surface_Line_Segment{
			start=alicorn.GPU_Surface_Point{tick.x, tick.y0},
			end=alicorn.GPU_Surface_Point{tick.x, tick.y1},
			thickness=tick.thickness,
			color=alicorn.Color{tick.color.r, tick.color.g, tick.color.b, tick.color.a},
		}
	}
	if !alicorn.gpu_surface_update_geometry(
		&view.runtime,
		view.ruler_geometry_id,
		geometry[:int(tick_count)],
		nil,
	) {
		return -1
	}
	presentation, ready := alicorn.begin_presentation_frame(&view.runtime)
	if !ready { return -1 }
	alicorn.end_presentation_frame(&presentation)
	return 1
}

@(export, link_name="audacity_alicorn_timeline_view_update_ruler_geometry")
audacity_alicorn_timeline_view_update_ruler_geometry :: proc "c" (
	opaque_view: rawptr,
	rectangle_input: ^AudacityWaveformRectangle,
	rectangle_count: u64,
	ticks: ^AudacityRulerTick,
	tick_count: u64,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Timeline_View)opaque_view
	if !valid_timeline_view(view) ||
	   (rectangle_count > 0 && rectangle_input == nil) ||
	   (tick_count > 0 && ticks == nil) ||
	   rectangle_count > u64(MAX_TIMELINE_GEOMETRY_RECTANGLES) ||
	   tick_count > u64(MAX_TIMELINE_GEOMETRY_SEGMENTS) ||
	   rectangle_count > u64(MAX_TIMELINE_GEOMETRY_SEGMENTS)-tick_count {
		return -1
	}
	rectangle_input_slice := mem.slice_ptr(rectangle_input, int(rectangle_count))
	tick_input := mem.slice_ptr(ticks, int(tick_count))
	for rectangle in rectangle_input_slice {
		if !alicorn.number_is_finite(rectangle.x) || !alicorn.number_is_finite(rectangle.y) ||
		   !alicorn.number_is_finite(rectangle.width) || !alicorn.number_is_finite(rectangle.height) ||
		   rectangle.width <= 0 || rectangle.height <= 0 ||
		   !timeline_color_valid(AudacitySnapColor{rectangle.r, rectangle.g, rectangle.b, rectangle.a}) {
			return -1
		}
	}
	for tick in tick_input {
		if !alicorn.number_is_finite(tick.x) || !alicorn.number_is_finite(tick.y0) ||
		   !alicorn.number_is_finite(tick.y1) || !alicorn.number_is_finite(tick.thickness) ||
		   tick.thickness <= 0 || !timeline_color_valid(tick.color) {
			return -1
		}
	}

	rectangles: [MAX_TIMELINE_GEOMETRY_RECTANGLES]alicorn.GPU_Surface_Filled_Rectangle
	for source, index in rectangle_input_slice {
		rectangles[index] = alicorn.GPU_Surface_Filled_Rectangle{
			bounds=alicorn.Rect{source.x, source.y, source.width, source.height},
			color=alicorn.Color{source.r, source.g, source.b, source.a},
		}
	}
	segments: [MAX_TIMELINE_GEOMETRY_SEGMENTS]alicorn.GPU_Surface_Line_Segment
	for source, index in tick_input {
		segments[index] = alicorn.GPU_Surface_Line_Segment{
			start=alicorn.GPU_Surface_Point{source.x, source.y0},
			end=alicorn.GPU_Surface_Point{source.x, source.y1},
			thickness=source.thickness,
			color=alicorn.Color{source.color.r, source.color.g, source.color.b, source.color.a},
		}
	}
	if !alicorn.gpu_surface_update_geometry_with_rectangles(
		&view.runtime,
		view.ruler_geometry_id,
		rectangles[:int(rectangle_count)],
		segments[:int(tick_count)],
		nil,
	) {
		return -1
	}
	presentation, ready := alicorn.begin_presentation_frame(&view.runtime)
	if !ready { return -1 }
	alicorn.end_presentation_frame(&presentation)
	return 1
}

@(export, link_name="audacity_alicorn_timeline_view_update_ruler_overlay")
audacity_alicorn_timeline_view_update_ruler_overlay :: proc "c" (
	opaque_view: rawptr,
	segment_input: ^AudacityWaveformSegment,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Timeline_View)opaque_view
	if !valid_timeline_view(view) { return -1 }
	segments: [1]alicorn.GPU_Surface_Line_Segment
	segment_count := 0
	if segment_input != nil {
		source := segment_input^
		if !alicorn.number_is_finite(source.x0) || !alicorn.number_is_finite(source.y0) ||
		   !alicorn.number_is_finite(source.x1) || !alicorn.number_is_finite(source.y1) ||
		   !alicorn.number_is_finite(source.thickness) || source.thickness <= 0 ||
		   !timeline_color_valid(AudacitySnapColor{source.r, source.g, source.b, source.a}) {
			return -1
		}
		segments[0] = alicorn.GPU_Surface_Line_Segment{
			start=alicorn.GPU_Surface_Point{source.x0, source.y0},
			end=alicorn.GPU_Surface_Point{source.x1, source.y1},
			thickness=source.thickness,
			color=alicorn.Color{source.r, source.g, source.b, source.a},
		}
		segment_count = 1
	}
	if !alicorn.gpu_surface_update_geometry(
		&view.runtime,
		view.ruler_overlay_geometry_id,
		segments[:segment_count],
		nil,
	) {
		return -1
	}
	presentation, ready := alicorn.begin_presentation_frame(&view.runtime)
	if !ready { return -1 }
	alicorn.end_presentation_frame(&presentation)
	return 1
}

@(export, link_name="audacity_alicorn_timeline_view_paint_waveform")
audacity_alicorn_timeline_view_paint_waveform :: proc "c" (
	opaque_view: rawptr,
	callback: AudacityWaveformPaintCallback,
	callback_context: rawptr,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Timeline_View)opaque_view
	if !valid_timeline_view(view) || callback == nil { return -1 }
	display, ok := alicorn.paint_display_list(&view.runtime)
	if !ok { return -1 }

	matched_labels := 0
	for command in display.commands {
		payload, is_text := command.payload.(alicorn.Text_Paint)
		if !is_text { continue }
		for i in 0..<view.waveform_label_text_count {
			if command.owner == view.waveform_label_text_ids[i] {
				_, run_ok := alicorn.paint_text_run_resolve(&view.runtime, payload.run)
				if !run_ok { return -1 }
				matched_labels += 1
				break
			}
		}
	}
	if matched_labels != view.waveform_label_text_count { return -1 }

	view.callback_active = true
	delivered := i32(0)
	for command in display.commands {
		if command.owner != view.waveform_geometry_id { continue }
		geometry, geometry_ok := command.payload.(alicorn.Geometry_Paint)
		if !geometry_ok { continue }
		payload, payload_ok := alicorn.gpu_surface_payload_resolve(&view.runtime, geometry.geometry)
		if !payload_ok || (len(payload.rectangles) == 0 && len(payload.segments) == 0 && len(payload.point_markers) == 0) { continue }

		output_rectangles: [MAX_TIMELINE_GEOMETRY_RECTANGLES]AudacityWaveformRectangle
		for i in 0..<len(payload.rectangles) {
			rectangle := payload.rectangles[i]
			output_rectangles[i] = AudacityWaveformRectangle{
				x=rectangle.bounds.x,
				y=rectangle.bounds.y,
				width=rectangle.bounds.w,
				height=rectangle.bounds.h,
				r=rectangle.color.r,
				g=rectangle.color.g,
				b=rectangle.color.b,
				a=rectangle.color.a,
			}
		}
		output: [MAX_TIMELINE_GEOMETRY_SEGMENTS]AudacityWaveformSegment
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
		output_markers: [MAX_TIMELINE_GEOMETRY_SEGMENTS]AudacityWaveformPointMarker
		for marker, i in payload.point_markers {
			output_markers[i] = AudacityWaveformPointMarker{
				center_x=marker.center.x,
				center_y=marker.center.y,
				diameter=marker.diameter,
				r=marker.color.r,
				g=marker.color.g,
				b=marker.color.b,
				a=marker.color.a,
			}
		}
		rectangle_pointer: ^AudacityWaveformRectangle = nil
		if len(payload.rectangles) > 0 { rectangle_pointer = &output_rectangles[0] }
		segment_pointer: ^AudacityWaveformSegment = nil
		if len(payload.segments) > 0 { segment_pointer = &output[0] }
		marker_pointer: ^AudacityWaveformPointMarker = nil
		if len(payload.point_markers) > 0 { marker_pointer = &output_markers[0] }
		callback(
			callback_context,
			rectangle_pointer,
			u64(len(payload.rectangles)),
			segment_pointer,
			u64(len(payload.segments)),
			marker_pointer,
			u64(len(payload.point_markers)),
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

@(export, link_name="audacity_alicorn_timeline_view_paint_clip_affordances")
audacity_alicorn_timeline_view_paint_clip_affordances :: proc "c" (
	opaque_view: rawptr,
	callback: AudacityWaveformPaintCallback,
	callback_context: rawptr,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Timeline_View)opaque_view
	if !valid_timeline_view(view) || callback == nil { return -1 }
	display, ok := alicorn.paint_display_list(&view.runtime)
	if !ok { return -1 }

	view.callback_active = true
	delivered := i32(0)
	for command in display.commands {
		if command.owner != view.clip_affordance_geometry_id { continue }
		geometry, geometry_ok := command.payload.(alicorn.Geometry_Paint)
		if !geometry_ok { continue }
		payload, payload_ok := alicorn.gpu_surface_payload_resolve(&view.runtime, geometry.geometry)
		if !payload_ok || (len(payload.rectangles) == 0 && len(payload.point_markers) == 0) { continue }

		output_rectangles: [MAX_TIMELINE_GEOMETRY_RECTANGLES]AudacityWaveformRectangle
		for source, index in payload.rectangles {
			output_rectangles[index] = AudacityWaveformRectangle{
				x=source.bounds.x,
				y=source.bounds.y,
				width=source.bounds.w,
				height=source.bounds.h,
				r=source.color.r,
				g=source.color.g,
				b=source.color.b,
				a=source.color.a,
			}
		}
		output_markers: [MAX_TIMELINE_GEOMETRY_SEGMENTS]AudacityWaveformPointMarker
		for source, index in payload.point_markers {
			output_markers[index] = AudacityWaveformPointMarker{
				center_x=source.center.x,
				center_y=source.center.y,
				diameter=source.diameter,
				r=source.color.r,
				g=source.color.g,
				b=source.color.b,
				a=source.color.a,
			}
		}
		rectangle_pointer: ^AudacityWaveformRectangle = nil
		if len(payload.rectangles) > 0 { rectangle_pointer = &output_rectangles[0] }
		marker_pointer: ^AudacityWaveformPointMarker = nil
		if len(payload.point_markers) > 0 { marker_pointer = &output_markers[0] }
		callback(
			callback_context,
			rectangle_pointer,
			u64(len(payload.rectangles)),
			nil,
			0,
			marker_pointer,
			u64(len(payload.point_markers)),
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

@(export, link_name="audacity_alicorn_timeline_view_paint_waveform_labels")
audacity_alicorn_timeline_view_paint_waveform_labels :: proc "c" (
	opaque_view: rawptr,
	callback: AudacitySnapDrawCallback,
	callback_context: rawptr,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Timeline_View)opaque_view
	if !valid_timeline_view(view) || callback == nil { return -1 }
	display, ok := alicorn.paint_display_list(&view.runtime)
	if !ok { return -1 }

	view.callback_active = true
	delivered := i32(0)
	for command in display.commands {
		payload, is_text := command.payload.(alicorn.Text_Paint)
		is_waveform_label := false
		for i in 0..<view.waveform_label_text_count {
			if command.owner == view.waveform_label_text_ids[i] {
				is_waveform_label = true
				break
			}
		}
		if !is_text || !is_waveform_label {
			continue
		}
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

@(export, link_name="audacity_alicorn_timeline_view_paint_ruler")
audacity_alicorn_timeline_view_paint_ruler :: proc "c" (
	opaque_view: rawptr,
	draw_callback: AudacityRulerDrawCallback,
	geometry_callback: AudacityWaveformPaintCallback,
	callback_context: rawptr,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Timeline_View)opaque_view
	if !valid_timeline_view(view) || draw_callback == nil || geometry_callback == nil { return -1 }
	display, ok := alicorn.paint_display_list(&view.runtime)
	if !ok { return -1 }

	view.callback_active = true
	delivered := i32(0)
	for command in display.commands {
		switch payload in command.payload {
		case alicorn.Text_Paint:
			if command.bounds.y < 0 || command.bounds.y >= view.ruler_height { continue }
			run, run_ok := alicorn.paint_text_run_resolve(&view.runtime, payload.run)
			if !run_ok { continue }
			output := snap_view_command_base(command, AUDACITY_SNAP_DRAW_TEXT)
			output.text_color = snap_color(payload.color)
			output.font_size = run.size
			output.text_length = u64(len(run.value))
			if len(run.value) > 0 { output.text_utf8 = raw_data(run.value) }
			draw_callback(callback_context, &output)
			delivered += 1
		case alicorn.Geometry_Paint:
			if command.owner != view.ruler_geometry_id { continue }
			geometry, geometry_ok := alicorn.gpu_surface_payload_resolve(&view.runtime, payload.geometry)
			if !geometry_ok || len(geometry.point_markers) != 0 ||
			   (len(geometry.rectangles) == 0 && len(geometry.segments) == 0) { continue }
			output_rectangles: [MAX_TIMELINE_GEOMETRY_RECTANGLES]AudacityWaveformRectangle
			for i in 0..<len(geometry.rectangles) {
				rectangle := geometry.rectangles[i]
				output_rectangles[i] = AudacityWaveformRectangle{
					x=rectangle.bounds.x,
					y=rectangle.bounds.y,
					width=rectangle.bounds.w,
					height=rectangle.bounds.h,
					r=rectangle.color.r,
					g=rectangle.color.g,
					b=rectangle.color.b,
					a=rectangle.color.a,
				}
			}
			output: [MAX_TIMELINE_GEOMETRY_SEGMENTS]AudacityWaveformSegment
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
			rectangle_pointer: ^AudacityWaveformRectangle = nil
			if len(geometry.rectangles) > 0 { rectangle_pointer = &output_rectangles[0] }
			segment_pointer: ^AudacityWaveformSegment = nil
			if len(geometry.segments) > 0 { segment_pointer = &output[0] }
			geometry_callback(
				callback_context,
				rectangle_pointer,
				u64(len(geometry.rectangles)),
				segment_pointer,
				u64(len(geometry.segments)),
				nil,
				0,
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

timeline_view_paint_single_segment_overlay :: proc(
	view: ^Timeline_View,
	owner: alicorn.Node_ID,
	geometry_callback: AudacityWaveformPaintCallback,
	callback_context: rawptr,
) -> i32 {
	if !valid_timeline_view(view) || geometry_callback == nil { return -1 }
	display, ok := alicorn.paint_display_list(&view.runtime)
	if !ok { return -1 }

	view.callback_active = true
	delivered := i32(0)
	for command in display.commands {
		if command.owner != owner { continue }
		geometry, geometry_ok := command.payload.(alicorn.Geometry_Paint)
		if !geometry_ok { continue }
		payload, payload_ok := alicorn.gpu_surface_payload_resolve(&view.runtime, geometry.geometry)
		if !payload_ok { continue }
		if len(payload.rectangles) != 0 || len(payload.segments) > 1 || len(payload.point_markers) != 0 {
			view.callback_active = false
			return -1
		}
		if len(payload.segments) == 0 { continue }

		segment := payload.segments[0]
		output := AudacityWaveformSegment{
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
		geometry_callback(
			callback_context,
			nil,
			0,
			&output,
			1,
			nil,
			0,
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

@(export, link_name="audacity_alicorn_timeline_view_paint_waveform_overlay")
audacity_alicorn_timeline_view_paint_waveform_overlay :: proc "c" (
	opaque_view: rawptr,
	indicator_kind: u32,
	geometry_callback: AudacityWaveformPaintCallback,
	callback_context: rawptr,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Timeline_View)opaque_view
	if view == nil { return -1 }
	indicator_id := timeline_waveform_indicator_id(view, indicator_kind)
	if indicator_id == 0 { return -1 }
	return timeline_view_paint_single_segment_overlay(
		view,
		indicator_id,
		geometry_callback,
		callback_context,
	)
}

@(export, link_name="audacity_alicorn_timeline_view_paint_ruler_overlay")
audacity_alicorn_timeline_view_paint_ruler_overlay :: proc "c" (
	opaque_view: rawptr,
	geometry_callback: AudacityWaveformPaintCallback,
	callback_context: rawptr,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Timeline_View)opaque_view
	if view == nil { return -1 }
	return timeline_view_paint_single_segment_overlay(
		view,
		view.ruler_overlay_geometry_id,
		geometry_callback,
		callback_context,
	)
}

// Return 1 with the currently pending output revision, 0 when this output is
// clean, and -1 for an invalid view/output. The caller acknowledges only after
// its native paint callback completes successfully.
@(export, link_name="audacity_alicorn_timeline_view_output_revision")
audacity_alicorn_timeline_view_output_revision :: proc "c" (
	opaque_view: rawptr,
	output_kind: u32,
	revision: ^u64,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Timeline_View)opaque_view
	if !valid_timeline_view(view) || revision == nil { return -1 }
	output, valid_output := timeline_output_id(view, output_kind)
	if !valid_output { return -1 }
	snapshot, status := alicorn.presentation_output_snapshot(&view.runtime, output)
	if status != .Ok { return -1 }
	if !snapshot.pending { return 0 }
	revision^ = snapshot.revision
	return 1
}

@(export, link_name="audacity_alicorn_timeline_view_output_submission_succeeded")
audacity_alicorn_timeline_view_output_submission_succeeded :: proc "c" (
	opaque_view: rawptr,
	output_kind: u32,
	revision: u64,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Timeline_View)opaque_view
	if !valid_timeline_view(view) { return -1 }
	output, valid_output := timeline_output_id(view, output_kind)
	if !valid_output { return -1 }
	status := alicorn.presentation_output_frame_submission_succeeded(&view.runtime, output, revision)
	if status == .Ok { return 1 }
	return -1
}
