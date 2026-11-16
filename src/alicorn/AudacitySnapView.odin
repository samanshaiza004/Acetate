package audacity_alicorn_snap_view

import alicorn "alicorn:runtime"
import "core:mem"
import odin_runtime "base:runtime"
import "core:strings"
import odin_time "core:time"

AudacitySnapRect :: struct {
	x, y, width, height: f32,
}

AudacitySnapColor :: struct {
	r, g, b, a: f32,
}

AudacitySnapDrawCommand :: struct {
	kind: u32,
	reserved: u32,
	owner: u64,
	bounds: AudacitySnapRect,
	clip: AudacitySnapRect,
	fill: AudacitySnapColor,
	stroke: AudacitySnapColor,
	text_color: AudacitySnapColor,
	stroke_width: f32,
	opacity: f32,
	translation_x: f32,
	translation_y: f32,
	radius: f32,
	radius_top_left: f32,
	radius_top_right: f32,
	radius_bottom_right: f32,
	radius_bottom_left: f32,
	shape_kind: u32,
	stroke_placement: u32,
	material_id: u32,
	icon_id: u32,
	text_utf8: ^u8,
	text_length: u64,
	font_size: f32,
}

AudacitySnapDrawCallback :: #type proc "c" (
	ctx: rawptr,
	command: ^AudacitySnapDrawCommand,
)

View :: struct {
	hosted: alicorn.Hosted_Runtime,
	checkbox_id: alicorn.Node_ID,
	width, height: int,
	pixel_width, pixel_height: int,
	dpi_scale: f32,
	checked: bool,
	last_checked: bool,
	checked_initialized: bool,
	fonts_initialized: bool,
	pending_change: bool,
	callback_active: bool,
	runtime_initialized: bool,
}

AUDACITY_SNAP_DRAW_SURFACE :: u32(1)
AUDACITY_SNAP_DRAW_ICON :: u32(2)
AUDACITY_SNAP_DRAW_TEXT :: u32(3)

AUDACITY_SNAP_SHAPE_RECTANGLE :: u32(1)
AUDACITY_SNAP_SHAPE_ROUNDED_RECTANGLE :: u32(2)
AUDACITY_SNAP_SHAPE_LINE :: u32(3)
AUDACITY_SNAP_SHAPE_PER_CORNER_ROUNDED_RECTANGLE :: u32(4)
AUDACITY_SNAP_SHAPE_CONNECTED_TAB :: u32(5)

AUDACITY_SNAP_ICON_NONE :: u32(0)
AUDACITY_SNAP_ICON_CHECK :: u32(1)
AUDACITY_SNAP_ICON_SETTINGS :: u32(2)
AUDACITY_SNAP_ICON_AUDIO_SETUP :: u32(3)
AUDACITY_SNAP_ICON_CUT :: u32(4)
AUDACITY_SNAP_ICON_COPY :: u32(5)
AUDACITY_SNAP_ICON_PASTE :: u32(6)
AUDACITY_SNAP_ICON_DELETE :: u32(7)
AUDACITY_SNAP_ICON_ZOOM_IN :: u32(8)
AUDACITY_SNAP_ICON_ZOOM_OUT :: u32(9)
AUDACITY_SNAP_ICON_ZOOM_SELECTION :: u32(10)
AUDACITY_SNAP_ICON_ZOOM_FIT :: u32(11)
AUDACITY_SNAP_ICON_ZOOM_TOGGLE :: u32(12)
AUDACITY_SNAP_ICON_TRIM :: u32(13)
AUDACITY_SNAP_ICON_SILENCE :: u32(14)
AUDACITY_SNAP_ICON_UNDO :: u32(15)
AUDACITY_SNAP_ICON_REDO :: u32(16)
AUDACITY_SNAP_ICON_PLAY :: u32(17)
AUDACITY_SNAP_ICON_STOP :: u32(18)
AUDACITY_SNAP_ICON_PLAY_CUT_PREVIEW :: u32(19)
AUDACITY_SNAP_ICON_PLAY_SCRUB :: u32(20)
AUDACITY_SNAP_ICON_PLAY_SEEK :: u32(21)
AUDACITY_SNAP_ICON_PAUSE :: u32(22)
AUDACITY_SNAP_ICON_REWIND :: u32(23)
AUDACITY_SNAP_ICON_FAST_FORWARD :: u32(24)
AUDACITY_SNAP_ICON_CHEVRON_DOWN :: u32(25)
AUDACITY_ACTION_ICON_NONE :: u32(0)
AUDACITY_ACTION_ICON_SETTINGS :: u32(1)
AUDACITY_ACTION_ICON_SETUP :: u32(2)
AUDACITY_ACTION_ICON_CUT :: u32(3)
AUDACITY_ACTION_ICON_COPY :: u32(4)
AUDACITY_ACTION_ICON_PASTE :: u32(5)
AUDACITY_ACTION_ICON_DELETE :: u32(6)
AUDACITY_ACTION_ICON_ZOOM_IN :: u32(7)
AUDACITY_ACTION_ICON_ZOOM_OUT :: u32(8)
AUDACITY_ACTION_ICON_ZOOM_SELECTION :: u32(9)
AUDACITY_ACTION_ICON_ZOOM_FIT :: u32(10)
AUDACITY_ACTION_ICON_ZOOM_TOGGLE :: u32(11)
AUDACITY_ACTION_ICON_TRIM :: u32(12)
AUDACITY_ACTION_ICON_SILENCE :: u32(13)
AUDACITY_ACTION_ICON_UNDO :: u32(14)
AUDACITY_ACTION_ICON_REDO :: u32(15)
AUDACITY_ACTION_ICON_PLAY :: u32(16)
AUDACITY_ACTION_ICON_STOP :: u32(17)
AUDACITY_ACTION_ICON_PLAY_CUT_PREVIEW :: u32(18)
AUDACITY_ACTION_ICON_PLAY_SCRUB :: u32(19)
AUDACITY_ACTION_ICON_PLAY_SEEK :: u32(20)
AUDACITY_ACTION_ICON_PAUSE :: u32(21)
AUDACITY_ACTION_ICON_REWIND :: u32(22)
AUDACITY_ACTION_ICON_FAST_FORWARD :: u32(23)

AUDACITY_SNAP_POINTER_MOVE :: u32(0)
AUDACITY_SNAP_POINTER_DOWN :: u32(1)
AUDACITY_SNAP_POINTER_UP :: u32(2)
AUDACITY_SNAP_POINTER_CANCEL :: u32(3)

AUDACITY_SNAP_KEY_TAB :: u32(0)
AUDACITY_SNAP_KEY_SHIFT_TAB :: u32(1)
AUDACITY_SNAP_KEY_SPACE :: u32(2)
AUDACITY_SNAP_KEY_ACTIVATE :: u32(3)

valid_view :: proc(view: ^View) -> bool {
	return view != nil && view.runtime_initialized && !view.callback_active
}

snap_color :: proc(color: alicorn.Color) -> AudacitySnapColor {
	return AudacitySnapColor{color.r, color.g, color.b, color.a}
}

snap_rect :: proc(rect: alicorn.Rect) -> AudacitySnapRect {
	return AudacitySnapRect{rect.x, rect.y, rect.w, rect.h}
}

snap_shape_kind :: proc(kind: alicorn.Surface_Shape_Kind) -> u32 {
	switch kind {
	case .Rectangle: return AUDACITY_SNAP_SHAPE_RECTANGLE
	case .Rounded_Rectangle: return AUDACITY_SNAP_SHAPE_ROUNDED_RECTANGLE
	case .Line: return AUDACITY_SNAP_SHAPE_LINE
	case .Rounded_Rectangle_Per_Corner: return AUDACITY_SNAP_SHAPE_PER_CORNER_ROUNDED_RECTANGLE
	case .Connected_Tab: return AUDACITY_SNAP_SHAPE_CONNECTED_TAB
	case .Circle, .Arc, .Cubic_Bezier: return 0
	}
	return 0
}

snap_view_render :: proc(state: rawptr, ui: ^alicorn.UI, width, height: int, dpi_scale: f32) {
	view := transmute(^View)state
	if view == nil || ui == nil { return }
	root_style := alicorn.layout_style(
		.Row,
		width=f32(width),
		height=f32(height),
		padding=2,
		align=.Start,
	)
	_ = alicorn.container_begin(
		ui,
		.Container,
		key=alicorn.key_string("audacity-alicorn-snap-root"),
		style=root_style,
	)
	control := alicorn.checkbox(
		ui,
		"Snap",
		view.checked,
		key=alicorn.key_string("audacity-alicorn-snap-checkbox"),
	)
	alicorn.container_end(ui)
	view.checked = control.value
	if control.changed { view.pending_change = true }
}

snap_view_post_wake :: proc(data: rawptr) -> bool {
	// This control starts no workers and never retains a Hosted_Waker.
	return false
}

snap_view_initialize :: proc(state: rawptr, rt: ^alicorn.Runtime) {
	view := transmute(^View)state
	if view != nil && rt != nil {
		view.fonts_initialized = alicorn.text_engine_load_default_fonts(rt)
	}
}

snap_view_now_ns :: proc() -> u64 {
	return u64(odin_time.tick_diff(odin_time.Tick{}, odin_time.tick_now()))
}

snap_view_step :: proc(view: ^View) -> alicorn.Hosted_Step_Result {
	result := alicorn.hosted_runtime_step(&view.hosted, snap_view_now_ns())
	if result.status != .Ok { return result }
	rt, status := alicorn.hosted_runtime_borrow(&view.hosted)
	if status != .Ok { result.status = status; return result }
	view.checkbox_id, _ = alicorn.node_by_key(
		rt,
		alicorn.key_string("audacity-alicorn-snap-checkbox"),
		.Checkbox,
	)
	return result
}

@(export, link_name="audacity_alicorn_snap_view_create")
audacity_alicorn_snap_view_create :: proc "c" (
	width, height: f32,
	pixel_width, pixel_height: i32,
	dpi_scale: f32,
	checked: u8,
) -> rawptr {
	context = odin_runtime.default_context()
	if !alicorn.number_is_finite(width) || !alicorn.number_is_finite(height) ||
		!alicorn.number_is_finite(dpi_scale) || width < 0 || height < 0 ||
		pixel_width < 0 || pixel_height < 0 || dpi_scale <= 0 {
		return nil
	}

	view := new(View)
	view.width = int(width)
	view.height = int(height)
	view.pixel_width = int(pixel_width)
	view.pixel_height = int(pixel_height)
	view.dpi_scale = dpi_scale
	view.last_checked = checked != 0
	view.checked_initialized = true
	view.checked = view.last_checked
	status := alicorn.hosted_runtime_init(&view.hosted, alicorn.Hosted_Runtime_Config{
		state=rawptr(view),
		logical_width=view.width,
		logical_height=view.height,
		pixel_width=int(pixel_width),
		pixel_height=int(pixel_height),
		dpi_scale=dpi_scale,
		render=snap_view_render,
		on_initialize=snap_view_initialize,
		post_wake=snap_view_post_wake,
	})
	if status != .Ok || !view.fonts_initialized {
		if status == .Ok { _ = alicorn.hosted_runtime_destroy(&view.hosted) }
		free(view)
		return nil
	}
	view.runtime_initialized = true
	result := snap_view_step(view)
	if result.status != .Ok {
		_ = alicorn.hosted_runtime_destroy(&view.hosted)
		view.runtime_initialized = false
		free(view)
		return nil
	}
	return rawptr(view)
}

@(export, link_name="audacity_alicorn_snap_view_destroy")
audacity_alicorn_snap_view_destroy :: proc "c" (opaque_view: rawptr) {
	if opaque_view == nil { return }
	context = odin_runtime.default_context()
	view := transmute(^View)opaque_view
	// A destroy requested from a draw callback is ignored rather than freeing
	// the storage currently borrowed by that callback. The caller may destroy
	// normally after the callback returns.
	if view.callback_active { return }
	if view.runtime_initialized {
		status := alicorn.hosted_runtime_destroy(&view.hosted)
		if status != .Ok { return }
		view.runtime_initialized = false
	}
	free(view)
}

@(export, link_name="audacity_alicorn_snap_view_resize")
audacity_alicorn_snap_view_resize :: proc "c" (
	opaque_view: rawptr,
	width, height: f32,
	pixel_width, pixel_height: i32,
	dpi_scale: f32,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^View)opaque_view
	if !valid_view(view) || !alicorn.number_is_finite(width) || !alicorn.number_is_finite(height) ||
		!alicorn.number_is_finite(dpi_scale) || width < 0 || height < 0 ||
		pixel_width < 0 || pixel_height < 0 || dpi_scale <= 0 {
		return -1
	}
	logical_width, logical_height := int(width), int(height)
	if view.width == logical_width && view.height == logical_height &&
		view.pixel_width == int(pixel_width) && view.pixel_height == int(pixel_height) &&
		view.dpi_scale == dpi_scale {
		return 0
	}
	status := alicorn.hosted_runtime_resize(
		&view.hosted,
		logical_width,
		logical_height,
		int(pixel_width),
		int(pixel_height),
		dpi_scale,
	)
	if status != .Ok { return -1 }
	view.width, view.height, view.dpi_scale = logical_width, logical_height, dpi_scale
	view.pixel_width, view.pixel_height = int(pixel_width), int(pixel_height)
	return 1 if snap_view_step(view).status == .Ok else -1
}

@(export, link_name="audacity_alicorn_snap_view_pointer")
audacity_alicorn_snap_view_pointer :: proc "c" (
	opaque_view: rawptr,
	kind: u32,
	x, y: f32,
	button: u32,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^View)opaque_view
	if !valid_view(view) || !alicorn.number_is_finite(x) || !alicorn.number_is_finite(y) {
		return -1
	}
	pointer_kind: alicorn.Pointer_Kind
	switch kind {
	case AUDACITY_SNAP_POINTER_MOVE: pointer_kind = .Move
	case AUDACITY_SNAP_POINTER_DOWN: pointer_kind = .Down
	case AUDACITY_SNAP_POINTER_UP: pointer_kind = .Up
	case AUDACITY_SNAP_POINTER_CANCEL: pointer_kind = .Cancel
	case: return -1
	}
	if kind == AUDACITY_SNAP_POINTER_MOVE || kind == AUDACITY_SNAP_POINTER_CANCEL {
		if button > 3 { return -1 }
	} else if button > 3 {
		return -1
	}
	target, status := alicorn.hosted_runtime_pointer(&view.hosted, alicorn.Pointer_Event{
		kind=pointer_kind,
		x=x,
		y=y,
		button=int(button),
	})
	if status != .Ok || snap_view_step(view).status != .Ok { return -1 }
	if target != 0 && target == view.checkbox_id { return 1 }
	return 0
}

@(export, link_name="audacity_alicorn_snap_view_key")
audacity_alicorn_snap_view_key :: proc "c" (opaque_view: rawptr, key: u32) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^View)opaque_view
	if !valid_view(view) { return -1 }
	switch key {
	case AUDACITY_SNAP_KEY_TAB:
		focused, status := alicorn.hosted_runtime_focus_traverse(&view.hosted, .Next)
		if status != .Ok || snap_view_step(view).status != .Ok { return -1 }
		return 1 if focused != 0 else 0
	case AUDACITY_SNAP_KEY_SHIFT_TAB:
		focused, status := alicorn.hosted_runtime_focus_traverse(&view.hosted, .Previous)
		if status != .Ok || snap_view_step(view).status != .Ok { return -1 }
		return 1 if focused != 0 else 0
	case AUDACITY_SNAP_KEY_SPACE:
		activated, status := alicorn.hosted_runtime_activate(&view.hosted, .Space)
		if status != .Ok || snap_view_step(view).status != .Ok { return -1 }
		return 1 if activated else 0
	case AUDACITY_SNAP_KEY_ACTIVATE:
		activated, status := alicorn.hosted_runtime_activate(&view.hosted, .Enter)
		if status != .Ok || snap_view_step(view).status != .Ok { return -1 }
		return 1 if activated else 0
	case:
		return -1
	}
}

@(export, link_name="audacity_alicorn_snap_view_focus_changed")
audacity_alicorn_snap_view_focus_changed :: proc "c" (
	opaque_view: rawptr,
	focused, focus_visible: u8,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^View)opaque_view
	if !valid_view(view) { return -1 }
	status := alicorn.hosted_runtime_focus_changed(
		&view.hosted,
		focused != 0,
		focus_visible != 0,
	)
	if status != .Ok || snap_view_step(view).status != .Ok { return -1 }
	return 1
}

@(export, link_name="audacity_alicorn_snap_view_update")
audacity_alicorn_snap_view_update :: proc "c" (
	opaque_view: rawptr,
	checked: u8,
	out_checked: ^u8,
	out_changed: ^u8,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^View)opaque_view
	if !valid_view(view) { return -1 }
	checked_value := checked != 0
	if out_checked != nil { out_checked^ = checked }
	if out_changed != nil { out_changed^ = 0 }
	if !view.pending_change && view.checked_initialized && view.last_checked != checked_value {
		view.checked = checked_value
		rt, status := alicorn.hosted_runtime_borrow(&view.hosted)
		if status != .Ok { return -1 }
		alicorn.invalidate_root(rt, "Audacity Snap state changed")
	}
	if !view.pending_change { view.checked = checked_value }
	view.last_checked = view.checked
	view.checked_initialized = true
	result := snap_view_step(view)
	if result.status != .Ok { return -1 }
	if out_checked != nil { out_checked^ = 1 if view.checked else 0 }
	if view.pending_change {
		if out_changed != nil { out_changed^ = 1 }
		view.pending_change = false
	}
	return 1 if result.description_built || result.render_required else 0
}

snap_view_command_base :: proc(command: alicorn.Paint_Command, kind: u32) -> AudacitySnapDrawCommand {
	return AudacitySnapDrawCommand{
		kind=kind,
		owner=u64(command.owner),
		bounds=snap_rect(command.bounds),
		clip=snap_rect(command.clip),
		opacity=command.opacity,
		translation_x=command.translation[0],
		translation_y=command.translation[1],
		stroke_placement=u32(command.stroke_placement),
	}
}

@(export, link_name="audacity_alicorn_snap_view_paint")
audacity_alicorn_snap_view_paint :: proc "c" (
	opaque_view: rawptr,
	callback: AudacitySnapDrawCallback,
	callback_context: rawptr,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^View)opaque_view
	if !valid_view(view) || callback == nil { return -1 }
	display, status := alicorn.hosted_runtime_display_commands(&view.hosted)
	if status != .Ok { return -1 }
	rt, borrow_status := alicorn.hosted_runtime_borrow(&view.hosted)
	if borrow_status != .Ok { return -1 }

	view.callback_active = true
	delivered := i32(0)
	for retained in display {
		if retained.owner != view.checkbox_id { continue }
		output := AudacitySnapDrawCommand{}
		switch payload in retained.payload {
		case alicorn.Surface_Paint:
			output = snap_view_command_base(retained, AUDACITY_SNAP_DRAW_SURFACE)
			output.fill = snap_color(payload.fill)
			output.stroke = snap_color(payload.stroke.color)
			output.stroke_width = payload.stroke.width
			output.radius = payload.shape.corner_radius
			output.shape_kind = snap_shape_kind(payload.shape.kind)
			output.material_id = u32(payload.material)
			if shape, shape_ok := alicorn.surface_paint_shape_resolve(rt, retained.owner, payload.shape); shape_ok {
				radii := alicorn.surface_shape_corner_radii(shape)
				output.radius_top_left = radii.top_left
				output.radius_top_right = radii.top_right
				output.radius_bottom_right = radii.bottom_right
				output.radius_bottom_left = radii.bottom_left
			}
		case alicorn.Icon_Paint:
			if payload.id != .Check { continue }
			output = snap_view_command_base(retained, AUDACITY_SNAP_DRAW_ICON)
			output.icon_id = AUDACITY_SNAP_ICON_CHECK
			output.fill = snap_color(payload.color)
		case alicorn.Text_Paint:
			run, run_ok := alicorn.paint_text_run_resolve(rt, payload.run)
			if !run_ok { continue }
			output = snap_view_command_base(retained, AUDACITY_SNAP_DRAW_TEXT)
			output.text_color = snap_color(payload.color)
			output.font_size = run.size
			output.text_length = u64(len(run.value))
			if len(run.value) > 0 { output.text_utf8 = raw_data(run.value) }
		case alicorn.Backdrop_Effect_Paint, alicorn.Image_Paint, alicorn.Geometry_Paint:
			continue
		case:
			continue
		}
		callback(callback_context, &output)
		delivered += 1
	}
	view.callback_active = false
	if alicorn.hosted_runtime_frame_submission_succeeded(&view.hosted) != .Ok { return -1 }
	return delivered
}

Action_View :: struct {
	hosted: alicorn.Hosted_Runtime,
	button_id: alicorn.Node_ID,
	icon_node_id: alicorn.Node_ID,
	width, height: int,
	label: string,
	icon_id: alicorn.Icon_ID,
	paint_icon_id: u32,
	last_enabled: bool,
	last_pressed: bool,
	state_initialized: bool,
	pending_activation: bool,
	fonts_initialized: bool,
	callback_active: bool,
	runtime_initialized: bool,
}

valid_action_view :: proc(view: ^Action_View) -> bool {
	return view != nil && view.runtime_initialized && !view.callback_active
}

action_view_build_description :: proc(
	view: ^Action_View,
	ui: ^alicorn.UI,
	width, height: int,
	enabled, pressed: bool,
) -> (built, activated: bool) {
	root_style := alicorn.layout_style(.Row, width=f32(width), height=f32(height), padding=2, align=.Center)
	_ = alicorn.container_begin(
		ui,
		.Container,
		key=alicorn.key_string("audacity-alicorn-action-root"),
		style=root_style,
	)
	button_style := alicorn.layout_style(.Row, width=max(0.0, f32(width)-4), height=max(0.0, f32(height)-4), align=.Center, justify=.Center)
	icon_size := f32(16)
	show_icon_label := view.paint_icon_id == AUDACITY_SNAP_ICON_AUDIO_SETUP
	if show_icon_label {
		button_style = alicorn.layout_style(.Column, width=max(0.0, f32(width)-4), height=max(0.0, f32(height)-4), align=.Center, justify=.Center, gap=1)
		icon_size = 24
	} else if view.paint_icon_id >= AUDACITY_SNAP_ICON_CUT {
		icon_size = 20
	}
	view.icon_node_id = 0
	if view.icon_id != .None {
		button_id, button_activated := alicorn.button_begin(
			ui,
			"",
			key=alicorn.key_string("audacity-alicorn-action-button"),
			style=button_style,
			state=alicorn.Button_State{selected=pressed, disabled=!enabled},
			content_style=alicorn.button_content_style(.Center, .Center, padding_x=0, padding_y=0),
			variant=.Default,
		)
		activated = button_activated
		if button_id != 0 {
			states := alicorn.Semantic_States{}
			actions := alicorn.Semantic_Actions{}
			if pressed { states = alicorn.semantic_states_add(states, .Selected) }
			if !enabled { states = alicorn.semantic_states_add(states, .Disabled) }
			if enabled { actions = alicorn.semantic_actions_add(actions, .Press) }
			_ = alicorn.semantic_description(ui, .Button, view.label, states=states, actions=actions)
			view.icon_node_id = alicorn.icon(ui, view.icon_id, key=alicorn.key_string("audacity-alicorn-action-icon"), size=icon_size, color_role=.Text)
			if show_icon_label {
				_ = alicorn.text(ui, view.label, key=alicorn.key_string("audacity-alicorn-action-label"))
			}
			alicorn.button_end(ui)
		}
	} else {
		activated = alicorn.button(
			ui,
			view.label,
			key=alicorn.key_string("audacity-alicorn-action-button"),
			style=button_style,
			state=alicorn.Button_State{selected=pressed, disabled=!enabled},
		)
	}
	alicorn.container_end(ui)
	return true, activated
}

action_view_render :: proc(
	state: rawptr,
	ui: ^alicorn.UI,
	logical_width, logical_height: int,
	dpi_scale: f32,
) {
	view := transmute(^Action_View)state
	if view == nil || ui == nil { return }
	_ = dpi_scale
	_, activated := action_view_build_description(
		view,
		ui,
		logical_width,
		logical_height,
		view.last_enabled,
		view.last_pressed,
	)
	view.pending_activation = view.pending_activation || activated
}

action_view_post_wake :: proc(data: rawptr) -> bool {
	_ = data
	return false
}

action_view_initialize :: proc(state: rawptr, rt: ^alicorn.Runtime) {
	view := transmute(^Action_View)state
	if view != nil && rt != nil {
		view.fonts_initialized = alicorn.text_engine_load_default_fonts(rt)
	}
}

action_view_step :: proc(view: ^Action_View) -> (result: alicorn.Hosted_Step_Result) {
	result = alicorn.hosted_runtime_step(&view.hosted, snap_view_now_ns())
	if result.status != .Ok { return }
	rt, status := alicorn.hosted_runtime_borrow(&view.hosted)
	if status != .Ok { result.status = status; return }
	view.button_id, _ = alicorn.node_by_key(
		rt,
		alicorn.key_string("audacity-alicorn-action-button"),
		.Button,
	)
	return
}

@(export, link_name="audacity_alicorn_action_view_create")
audacity_alicorn_action_view_create :: proc "c" (
	width, height: f32,
	label_utf8: cstring,
	enabled, pressed: u8,
	icon_id: u32,
) -> rawptr {
	context = odin_runtime.default_context()
	if !alicorn.number_is_finite(width) || !alicorn.number_is_finite(height) || width < 0 || height < 0 || label_utf8 == nil {
		return nil
	}
	label := string(label_utf8)
	if len(label) == 0 || len(label) > 256 { return nil }
	icon: alicorn.Icon_ID
	paint_icon_id := u32(AUDACITY_SNAP_ICON_NONE)
	switch icon_id {
	case AUDACITY_ACTION_ICON_NONE: icon = .None
	case AUDACITY_ACTION_ICON_SETTINGS:
		icon = .Settings
		paint_icon_id = AUDACITY_SNAP_ICON_SETTINGS
	case AUDACITY_ACTION_ICON_SETUP:
		icon = .Settings
		paint_icon_id = AUDACITY_SNAP_ICON_AUDIO_SETUP
	case AUDACITY_ACTION_ICON_CUT:
		icon = .Settings
		paint_icon_id = AUDACITY_SNAP_ICON_CUT
	case AUDACITY_ACTION_ICON_COPY:
		icon = .Settings
		paint_icon_id = AUDACITY_SNAP_ICON_COPY
	case AUDACITY_ACTION_ICON_PASTE:
		icon = .Settings
		paint_icon_id = AUDACITY_SNAP_ICON_PASTE
	case AUDACITY_ACTION_ICON_DELETE:
		icon = .Settings
		paint_icon_id = AUDACITY_SNAP_ICON_DELETE
	case AUDACITY_ACTION_ICON_ZOOM_IN:
		icon = .Settings
		paint_icon_id = AUDACITY_SNAP_ICON_ZOOM_IN
	case AUDACITY_ACTION_ICON_ZOOM_OUT:
		icon = .Settings
		paint_icon_id = AUDACITY_SNAP_ICON_ZOOM_OUT
	case AUDACITY_ACTION_ICON_ZOOM_SELECTION:
		icon = .Settings
		paint_icon_id = AUDACITY_SNAP_ICON_ZOOM_SELECTION
	case AUDACITY_ACTION_ICON_ZOOM_FIT:
		icon = .Settings
		paint_icon_id = AUDACITY_SNAP_ICON_ZOOM_FIT
	case AUDACITY_ACTION_ICON_ZOOM_TOGGLE:
		icon = .Settings
		paint_icon_id = AUDACITY_SNAP_ICON_ZOOM_TOGGLE
	case AUDACITY_ACTION_ICON_TRIM:
		icon = .Settings
		paint_icon_id = AUDACITY_SNAP_ICON_TRIM
	case AUDACITY_ACTION_ICON_SILENCE:
		icon = .Settings
		paint_icon_id = AUDACITY_SNAP_ICON_SILENCE
	case AUDACITY_ACTION_ICON_UNDO:
		icon = .Settings
		paint_icon_id = AUDACITY_SNAP_ICON_UNDO
	case AUDACITY_ACTION_ICON_REDO:
		icon = .Settings
		paint_icon_id = AUDACITY_SNAP_ICON_REDO
	case AUDACITY_ACTION_ICON_PLAY:
		icon = .Settings
		paint_icon_id = AUDACITY_SNAP_ICON_PLAY
	case AUDACITY_ACTION_ICON_PLAY_CUT_PREVIEW:
		icon = .Settings
		paint_icon_id = AUDACITY_SNAP_ICON_PLAY_CUT_PREVIEW
	case AUDACITY_ACTION_ICON_PLAY_SCRUB:
		icon = .Settings
		paint_icon_id = AUDACITY_SNAP_ICON_PLAY_SCRUB
	case AUDACITY_ACTION_ICON_PLAY_SEEK:
		icon = .Settings
		paint_icon_id = AUDACITY_SNAP_ICON_PLAY_SEEK
	case AUDACITY_ACTION_ICON_STOP:
		icon = .Settings
		paint_icon_id = AUDACITY_SNAP_ICON_STOP
	case AUDACITY_ACTION_ICON_PAUSE:
		icon = .Settings
		paint_icon_id = AUDACITY_SNAP_ICON_PAUSE
	case AUDACITY_ACTION_ICON_REWIND:
		icon = .Settings
		paint_icon_id = AUDACITY_SNAP_ICON_REWIND
	case AUDACITY_ACTION_ICON_FAST_FORWARD:
		icon = .Settings
		paint_icon_id = AUDACITY_SNAP_ICON_FAST_FORWARD
	case: return nil
	}
	owned_label, clone_error := strings.clone(label, context.allocator)
	if clone_error != nil { return nil }

	view := new(Action_View)
	view.width, view.height = int(width), int(height)
	view.label = owned_label
	view.icon_id = icon
	view.paint_icon_id = paint_icon_id
	view.last_enabled = enabled != 0
	view.last_pressed = pressed != 0
	view.state_initialized = true
	status := alicorn.hosted_runtime_init(&view.hosted, alicorn.Hosted_Runtime_Config{
		state=rawptr(view),
		logical_width=view.width,
		logical_height=view.height,
		pixel_width=view.width,
		pixel_height=view.height,
		dpi_scale=1,
		render=action_view_render,
		on_initialize=action_view_initialize,
		post_wake=action_view_post_wake,
	})
	if status != .Ok {
		delete(view.label, context.allocator)
		free(view)
		return nil
	}
	view.runtime_initialized = true
	result := action_view_step(view)
	if !view.fonts_initialized || result.status != .Ok || view.button_id == 0 {
		_ = alicorn.hosted_runtime_destroy(&view.hosted)
		view.runtime_initialized = false
		delete(view.label, context.allocator)
		free(view)
		return nil
	}
	return rawptr(view)
}

@(export, link_name="audacity_alicorn_action_view_set_icon")
audacity_alicorn_action_view_set_icon :: proc "c" (
	opaque_view: rawptr,
	icon_id: u32,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Action_View)opaque_view
	if !valid_action_view(view) { return -1 }
	paint_icon_id: u32
	switch icon_id {
	case AUDACITY_ACTION_ICON_PLAY:
		paint_icon_id = AUDACITY_SNAP_ICON_PLAY
	case AUDACITY_ACTION_ICON_PLAY_CUT_PREVIEW:
		paint_icon_id = AUDACITY_SNAP_ICON_PLAY_CUT_PREVIEW
	case AUDACITY_ACTION_ICON_PLAY_SCRUB:
		paint_icon_id = AUDACITY_SNAP_ICON_PLAY_SCRUB
	case AUDACITY_ACTION_ICON_PLAY_SEEK:
		paint_icon_id = AUDACITY_SNAP_ICON_PLAY_SEEK
	case:
		return -1
	}
	view.paint_icon_id = paint_icon_id
	return 1
}

@(export, link_name="audacity_alicorn_action_view_destroy")
audacity_alicorn_action_view_destroy :: proc "c" (opaque_view: rawptr) {
	if opaque_view == nil { return }
	context = odin_runtime.default_context()
	view := transmute(^Action_View)opaque_view
	if view.callback_active { return }
	if view.runtime_initialized {
		if alicorn.hosted_runtime_destroy(&view.hosted) != .Ok { return }
		view.runtime_initialized = false
	}
	delete(view.label, context.allocator)
	free(view)
}

@(export, link_name="audacity_alicorn_action_view_resize")
audacity_alicorn_action_view_resize :: proc "c" (opaque_view: rawptr, width, height: f32) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Action_View)opaque_view
	if !valid_action_view(view) || !alicorn.number_is_finite(width) || !alicorn.number_is_finite(height) || width < 0 || height < 0 {
		return -1
	}
	logical_width, logical_height := int(width), int(height)
	if view.width == logical_width && view.height == logical_height { return 0 }
	status := alicorn.hosted_runtime_resize(
		&view.hosted,
		logical_width,
		logical_height,
		logical_width,
		logical_height,
		1,
	)
	if status != .Ok { return -1 }
	view.width, view.height = logical_width, logical_height
	return 1 if action_view_step(view).status == .Ok else -1
}

@(export, link_name="audacity_alicorn_action_view_pointer")
audacity_alicorn_action_view_pointer :: proc "c" (
	opaque_view: rawptr,
	kind: u32,
	x, y: f32,
	button: u32,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Action_View)opaque_view
	if !valid_action_view(view) || !alicorn.number_is_finite(x) || !alicorn.number_is_finite(y) || button > 3 { return -1 }
	pointer_kind: alicorn.Pointer_Kind
	switch kind {
	case AUDACITY_SNAP_POINTER_MOVE: pointer_kind = .Move
	case AUDACITY_SNAP_POINTER_DOWN: pointer_kind = .Down
	case AUDACITY_SNAP_POINTER_UP: pointer_kind = .Up
	case AUDACITY_SNAP_POINTER_CANCEL: pointer_kind = .Cancel
	case: return -1
	}
	target, status := alicorn.hosted_runtime_pointer(&view.hosted, alicorn.Pointer_Event{
		kind=pointer_kind,
		x=x,
		y=y,
		button=int(button),
	})
	if status != .Ok || action_view_step(view).status != .Ok { return -1 }
	return 1 if target != 0 && target == view.button_id else 0
}

@(export, link_name="audacity_alicorn_action_view_key")
audacity_alicorn_action_view_key :: proc "c" (opaque_view: rawptr, key: u32) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Action_View)opaque_view
	if !valid_action_view(view) { return -1 }
	switch key {
	case AUDACITY_SNAP_KEY_TAB:
		focused, status := alicorn.hosted_runtime_focus_traverse(&view.hosted, .Next)
		if status != .Ok || action_view_step(view).status != .Ok { return -1 }
		return 1 if focused != 0 else 0
	case AUDACITY_SNAP_KEY_SHIFT_TAB:
		focused, status := alicorn.hosted_runtime_focus_traverse(&view.hosted, .Previous)
		if status != .Ok || action_view_step(view).status != .Ok { return -1 }
		return 1 if focused != 0 else 0
	case AUDACITY_SNAP_KEY_SPACE:
		activated, status := alicorn.hosted_runtime_activate(&view.hosted, .Space)
		if status != .Ok || action_view_step(view).status != .Ok { return -1 }
		view.pending_activation = view.pending_activation || activated
		return 1 if activated else 0
	case AUDACITY_SNAP_KEY_ACTIVATE:
		activated, status := alicorn.hosted_runtime_activate(&view.hosted, .Enter)
		if status != .Ok || action_view_step(view).status != .Ok { return -1 }
		view.pending_activation = view.pending_activation || activated
		return 1 if activated else 0
	case:
		return -1
	}
}

@(export, link_name="audacity_alicorn_action_view_update")
audacity_alicorn_action_view_update :: proc "c" (
	opaque_view: rawptr,
	enabled, pressed: u8,
	out_activated: ^u8,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Action_View)opaque_view
	if !valid_action_view(view) { return -1 }
	enabled_value, pressed_value := enabled != 0, pressed != 0
	if out_activated != nil { out_activated^ = 0 }
	if view.state_initialized && (view.last_enabled != enabled_value || view.last_pressed != pressed_value) {
		rt, status := alicorn.hosted_runtime_borrow(&view.hosted)
		if status != .Ok { return -1 }
		alicorn.invalidate_root(rt, "Audacity action button state changed")
	}
	view.last_enabled, view.last_pressed = enabled_value, pressed_value
	view.state_initialized = true
	result := action_view_step(view)
	if result.status != .Ok { return -1 }
	if view.pending_activation {
		if out_activated != nil { out_activated^ = 1 }
		view.pending_activation = false
	}
	return 1 if result.description_built || result.render_required else 0
}

@(export, link_name="audacity_alicorn_action_view_paint")
audacity_alicorn_action_view_paint :: proc "c" (
	opaque_view: rawptr,
	callback: AudacitySnapDrawCallback,
	callback_context: rawptr,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Action_View)opaque_view
	if !valid_action_view(view) || callback == nil { return -1 }
	display, status := alicorn.hosted_runtime_display_commands(&view.hosted)
	if status != .Ok { return -1 }
	rt, borrow_status := alicorn.hosted_runtime_borrow(&view.hosted)
	if borrow_status != .Ok { return -1 }
	view.callback_active = true
	delivered := i32(0)
	for retained in display {
		if retained.owner != view.button_id && retained.owner != view.icon_node_id { continue }
		output := AudacitySnapDrawCommand{}
		switch payload in retained.payload {
		case alicorn.Surface_Paint:
			output = snap_view_command_base(retained, AUDACITY_SNAP_DRAW_SURFACE)
			output.fill = snap_color(payload.fill)
			output.stroke = snap_color(payload.stroke.color)
			output.stroke_width = payload.stroke.width
			output.radius = payload.shape.corner_radius
			output.shape_kind = snap_shape_kind(payload.shape.kind)
			output.material_id = u32(payload.material)
		case alicorn.Icon_Paint:
			if payload.id != .Settings { continue }
			output = snap_view_command_base(retained, AUDACITY_SNAP_DRAW_ICON)
			output.icon_id = view.paint_icon_id
			output.fill = snap_color(payload.color)
		case alicorn.Text_Paint:
			run, run_ok := alicorn.paint_text_run_resolve(rt, payload.run)
			if !run_ok { continue }
			output = snap_view_command_base(retained, AUDACITY_SNAP_DRAW_TEXT)
			output.text_color = snap_color(payload.color)
			output.font_size = run.size
			output.text_length = u64(len(run.value))
			if len(run.value) > 0 { output.text_utf8 = raw_data(run.value) }
		case alicorn.Backdrop_Effect_Paint, alicorn.Image_Paint, alicorn.Geometry_Paint:
			continue
		case:
			continue
		}
		callback(callback_context, &output)
		delivered += 1
	}
	view.callback_active = false
	if alicorn.hosted_runtime_frame_submission_succeeded(&view.hosted) != .Ok { return -1 }
	return delivered
}

AudacityNumericTimeSegment :: struct {
	text_utf8: ^u8,
	text_length: u64,
	width: f32,
	digit_index: i32,
}

AudacityNumericTimeStyle :: struct {
	background: AudacitySnapColor,
	digit_background: AudacitySnapColor,
	focused_digit_background: AudacitySnapColor,
	text_color: AudacitySnapColor,
	focused_digit_text_color: AudacitySnapColor,
	focus_border: AudacitySnapColor,
	menu_icon: AudacitySnapColor,
	width, height, content_width, digit_height, menu_width: f32,
	digit_font_size, label_font_size: f32,
	focused_digit: i32,
	focused, enabled, menu_enabled, reserved: u8,
}

Numeric_Time_Segment :: struct {
	text: string,
	width: f32,
	digit_index: i32,
}

Numeric_Time_View :: struct {
	hosted: alicorn.Hosted_Runtime,
	width, height: int,
	pixel_width, pixel_height: int,
	dpi_scale: f32,
	segments: []Numeric_Time_Segment,
	style: AudacityNumericTimeStyle,
	focused_digit: i32,
	digit_count: i32,
	style_initialized: bool,
	fonts_initialized: bool,
	callback_active: bool,
	runtime_initialized: bool,
}

NUMERIC_TIME_MAX_SEGMENTS :: 128
NUMERIC_TIME_MAX_TEXT_BYTES :: 4096

AUDACITY_NUMERIC_TIME_KEY_LEFT :: u32(1)
AUDACITY_NUMERIC_TIME_KEY_RIGHT :: u32(2)
AUDACITY_NUMERIC_TIME_KEY_HOME :: u32(3)
AUDACITY_NUMERIC_TIME_KEY_END :: u32(4)

valid_numeric_time_view :: proc(view: ^Numeric_Time_View) -> bool {
	return view != nil && view.runtime_initialized && !view.callback_active
}

numeric_time_color_equal :: proc(a, b: AudacitySnapColor) -> bool {
	return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a
}

numeric_time_color :: proc(color: AudacitySnapColor) -> alicorn.Color {
	return alicorn.Color{r=color.r, g=color.g, b=color.b, a=color.a}
}

numeric_time_style_equal :: proc(a, b: AudacityNumericTimeStyle) -> bool {
	return numeric_time_color_equal(a.background, b.background) &&
		numeric_time_color_equal(a.digit_background, b.digit_background) &&
		numeric_time_color_equal(a.focused_digit_background, b.focused_digit_background) &&
		numeric_time_color_equal(a.text_color, b.text_color) &&
		numeric_time_color_equal(a.focused_digit_text_color, b.focused_digit_text_color) &&
		numeric_time_color_equal(a.focus_border, b.focus_border) &&
		numeric_time_color_equal(a.menu_icon, b.menu_icon) &&
		a.width == b.width && a.height == b.height && a.content_width == b.content_width &&
		a.digit_height == b.digit_height && a.menu_width == b.menu_width &&
		a.digit_font_size == b.digit_font_size && a.label_font_size == b.label_font_size &&
		a.focused_digit == b.focused_digit && a.focused == b.focused &&
		a.enabled == b.enabled && a.menu_enabled == b.menu_enabled
}

numeric_time_segments_equal :: proc(a, b: []Numeric_Time_Segment) -> bool {
	if len(a) != len(b) { return false }
	for i in 0..<len(a) {
		if a[i].text != b[i].text || a[i].width != b[i].width || a[i].digit_index != b[i].digit_index {
			return false
		}
	}
	return true
}

numeric_time_segments_destroy :: proc(segments: []Numeric_Time_Segment, allocator: mem.Allocator) {
	for segment in segments { delete(segment.text, allocator) }
	delete(segments, allocator)
}

numeric_time_render :: proc(state: rawptr, ui: ^alicorn.UI, width, height: int, dpi_scale: f32) {
	view := transmute(^Numeric_Time_View)state
	if view == nil || ui == nil { return }
	_ = dpi_scale
	style := view.style
	outer_stroke := alicorn.Paint_Stroke{}
	if style.focused != 0 {
		outer_stroke = alicorn.Paint_Stroke{color=numeric_time_color(style.focus_border), width=1}
	}
	root := alicorn.surface_begin(
		ui,
		alicorn.surface_core_color_role(.Surface),
		key=alicorn.key_string("audacity-alicorn-numeric-time-root"),
		style=alicorn.layout_style(.Row, width=f32(width), height=f32(height), align=.Baseline),
		fill=numeric_time_color(style.background),
		stroke=outer_stroke,
		stroke_placement=.Inside,
		label="Audacity time value",
	)
	if root == 0 { return }

	content := alicorn.container_begin(
		ui,
		.Container,
		key=alicorn.key_string("audacity-alicorn-numeric-time-content"),
		style=alicorn.layout_style(.Row, width=style.content_width, height=f32(height), padding=1, align=.Baseline),
	)
	if content != 0 {
		for index in 0..<len(view.segments) {
			segment := view.segments[index]
			if segment.digit_index >= 0 {
				focused_digit := style.focused != 0 && segment.digit_index == style.focused_digit
				fill := numeric_time_color(style.digit_background)
				text_color := numeric_time_color(style.text_color)
				if focused_digit {
					fill = numeric_time_color(style.focused_digit_background)
					text_color = numeric_time_color(style.focused_digit_text_color)
				}
				digit := alicorn.surface_begin(
					ui,
					alicorn.surface_core_color_role(.Surface),
					key=alicorn.key_u64(u64(index+1)),
					style=alicorn.layout_style(.Column, width=segment.width, height=style.digit_height, align=.Center, justify=.Center),
					fill=fill,
					label="Time digit",
				)
				if digit != 0 {
					text_style := alicorn.Text_Style{font_size=style.digit_font_size, overflow=.Clip}
					_ = alicorn.text(
						ui,
						segment.text,
						key=alicorn.key_pair(u64(index+1), 0xD1617),
						style=alicorn.layout_style(.Row, width=segment.width, height=style.digit_height, align=.Center, justify=.Center),
						text_style=text_style,
						color=text_color,
					)
					alicorn.surface_end(ui)
				}
			} else {
				literal := alicorn.container_begin(
					ui,
					.Container,
					key=alicorn.key_u64(u64(index+1) | 0x8000_0000_0000_0000),
					style=alicorn.layout_style(.Column, width=segment.width, height=f32(height), align=.Center, justify=.Center),
				)
				if literal != 0 {
					text_style := alicorn.Text_Style{font_size=style.label_font_size, overflow=.Clip}
					_ = alicorn.text(
						ui,
						segment.text,
						key=alicorn.key_pair(u64(index+1), 0x117E2A1),
						style=alicorn.layout_style(.Row, width=segment.width, height=f32(height), align=.Center, justify=.Center),
						text_style=text_style,
						color=numeric_time_color(style.text_color),
					)
					alicorn.container_end(ui)
				}
			}
		}
		if style.menu_enabled != 0 && style.menu_width > 0 {
			menu := alicorn.container_begin(
				ui,
				.Container,
				key=alicorn.key_string("audacity-alicorn-numeric-time-menu"),
				style=alicorn.layout_style(.Row, width=style.menu_width, height=f32(height), align=.Center, justify=.Center),
			)
			if menu != 0 {
				_ = alicorn.icon(ui, .Chevron_Down, key=alicorn.key_string("audacity-alicorn-numeric-time-chevron"), size=min(10.0, f32(height)*0.45), color_role=.Muted_Text)
				alicorn.container_end(ui)
			}
		}
		alicorn.container_end(ui)
	}
	alicorn.surface_end(ui)
}

numeric_time_post_wake :: proc(data: rawptr) -> bool {
	_ = data
	return false
}

numeric_time_on_key :: proc(
	state: rawptr,
	rt: ^alicorn.Runtime,
	focused_owner: alicorn.Node_ID,
	event: alicorn.Hosted_Key_Event,
) -> bool {
	_ = rt
	_ = focused_owner
	view := transmute(^Numeric_Time_View)state
	if view == nil || !event.pressed || view.digit_count <= 0 {
		return false
	}
	index := clamp(view.focused_digit, 0, view.digit_count-1)
	switch event.key_code {
	case AUDACITY_NUMERIC_TIME_KEY_LEFT:
		index = (index + view.digit_count - 1) % view.digit_count
	case AUDACITY_NUMERIC_TIME_KEY_RIGHT:
		index = (index + 1) % view.digit_count
	case AUDACITY_NUMERIC_TIME_KEY_HOME:
		index = 0
	case AUDACITY_NUMERIC_TIME_KEY_END:
		index = view.digit_count - 1
	case:
		return false
	}
	view.focused_digit = index
	view.style.focused_digit = index
	return true
}

numeric_time_initialize :: proc(state: rawptr, rt: ^alicorn.Runtime) {
	view := transmute(^Numeric_Time_View)state
	if view != nil && rt != nil {
		view.fonts_initialized = alicorn.text_engine_load_default_fonts(rt)
	}
}

numeric_time_now_ns :: proc() -> u64 {
	return u64(odin_time.tick_diff(odin_time.Tick{}, odin_time.tick_now()))
}

numeric_time_step :: proc(view: ^Numeric_Time_View) -> alicorn.Hosted_Step_Result {
	return alicorn.hosted_runtime_step(&view.hosted, numeric_time_now_ns())
}

@(export, link_name="audacity_alicorn_numeric_time_view_create")
audacity_alicorn_numeric_time_view_create :: proc "c" (
	width, height: f32,
	pixel_width, pixel_height: i32,
	dpi_scale: f32,
) -> rawptr {
	context = odin_runtime.default_context()
	if !alicorn.number_is_finite(width) || !alicorn.number_is_finite(height) ||
		!alicorn.number_is_finite(dpi_scale) || width < 0 || height < 0 ||
		pixel_width < 0 || pixel_height < 0 || dpi_scale <= 0 {
		return nil
	}
	view := new(Numeric_Time_View)
	view.width, view.height = int(width), int(height)
	view.pixel_width, view.pixel_height = int(pixel_width), int(pixel_height)
	view.dpi_scale = dpi_scale
	status := alicorn.hosted_runtime_init(&view.hosted, alicorn.Hosted_Runtime_Config{
		state=rawptr(view),
		logical_width=view.width,
		logical_height=view.height,
		pixel_width=view.pixel_width,
		pixel_height=view.pixel_height,
		dpi_scale=dpi_scale,
		render=numeric_time_render,
		on_initialize=numeric_time_initialize,
		post_wake=numeric_time_post_wake,
		on_key=numeric_time_on_key,
	})
	if status != .Ok || !view.fonts_initialized {
		if status == .Ok { _ = alicorn.hosted_runtime_destroy(&view.hosted) }
		free(view)
		return nil
	}
	view.runtime_initialized = true
	if numeric_time_step(view).status != .Ok {
		_ = alicorn.hosted_runtime_destroy(&view.hosted)
		view.runtime_initialized = false
		free(view)
		return nil
	}
	return rawptr(view)
}

@(export, link_name="audacity_alicorn_numeric_time_view_destroy")
audacity_alicorn_numeric_time_view_destroy :: proc "c" (opaque_view: rawptr) {
	if opaque_view == nil { return }
	context = odin_runtime.default_context()
	view := transmute(^Numeric_Time_View)opaque_view
	if view.callback_active { return }
	if view.runtime_initialized {
		_ = alicorn.hosted_runtime_destroy(&view.hosted)
		view.runtime_initialized = false
	}
	numeric_time_segments_destroy(view.segments, context.allocator)
	free(view)
}

@(export, link_name="audacity_alicorn_numeric_time_view_resize")
audacity_alicorn_numeric_time_view_resize :: proc "c" (
	opaque_view: rawptr,
	width, height: f32,
	pixel_width, pixel_height: i32,
	dpi_scale: f32,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Numeric_Time_View)opaque_view
	if !valid_numeric_time_view(view) || !alicorn.number_is_finite(width) ||
		!alicorn.number_is_finite(height) || !alicorn.number_is_finite(dpi_scale) ||
		width < 0 || height < 0 || pixel_width < 0 || pixel_height < 0 || dpi_scale <= 0 {
		return -1
	}
	if view.width == int(width) && view.height == int(height) &&
		view.pixel_width == int(pixel_width) && view.pixel_height == int(pixel_height) &&
		view.dpi_scale == dpi_scale {
		return 0
	}
	status := alicorn.hosted_runtime_resize(
		&view.hosted,
		int(width), int(height), int(pixel_width), int(pixel_height), dpi_scale,
	)
	if status != .Ok { return -1 }
	view.width, view.height = int(width), int(height)
	view.pixel_width, view.pixel_height = int(pixel_width), int(pixel_height)
	view.dpi_scale = dpi_scale
	return 1 if numeric_time_step(view).status == .Ok else -1
}

@(export, link_name="audacity_alicorn_numeric_time_view_update")
audacity_alicorn_numeric_time_view_update :: proc "c" (
	opaque_view: rawptr,
	input_segments: ^AudacityNumericTimeSegment,
	segment_count: u64,
	input_style: ^AudacityNumericTimeStyle,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Numeric_Time_View)opaque_view
	if !valid_numeric_time_view(view) || input_style == nil || segment_count > NUMERIC_TIME_MAX_SEGMENTS ||
		(segment_count > 0 && input_segments == nil) {
		return -1
	}
	style := input_style^
	if !alicorn.number_is_finite(style.width) || !alicorn.number_is_finite(style.height) ||
		!alicorn.number_is_finite(style.content_width) || !alicorn.number_is_finite(style.digit_height) ||
		!alicorn.number_is_finite(style.menu_width) || !alicorn.number_is_finite(style.digit_font_size) ||
		!alicorn.number_is_finite(style.label_font_size) || style.width < 0 || style.height < 0 ||
		style.content_width < 0 || style.digit_height < 0 || style.menu_width < 0 ||
		style.digit_font_size < 0 || style.label_font_size < 0 || style.digit_font_size > 256 ||
		style.label_font_size > 256 || style.enabled > 1 || style.focused > 1 || style.menu_enabled > 1 {
		return -1
	}

	candidate := make([]Numeric_Time_Segment, int(segment_count), allocator=context.allocator)
	input := mem.slice_ptr(input_segments, int(segment_count))
	total_bytes := 0
	for i in 0..<len(input) {
		segment := input[i]
		if !alicorn.number_is_finite(segment.width) || segment.width < 0 || segment.width > 4096 ||
			segment.text_length > 1024 || (segment.text_length > 0 && segment.text_utf8 == nil) ||
			segment.digit_index < -1 {
			numeric_time_segments_destroy(candidate, context.allocator)
			return -1
		}
		total_bytes += int(segment.text_length)
		if total_bytes > NUMERIC_TIME_MAX_TEXT_BYTES {
			numeric_time_segments_destroy(candidate, context.allocator)
			return -1
		}
		value := ""
		if segment.text_length > 0 {
			value = strings.clone(string(mem.slice_ptr(segment.text_utf8, int(segment.text_length))), context.allocator)
		}
		candidate[i] = Numeric_Time_Segment{text=value, width=segment.width, digit_index=segment.digit_index}
	}
	digit_count := i32(0)
	for segment in candidate {
		if segment.digit_index >= digit_count {
			digit_count = segment.digit_index + 1
		}
	}
	view.digit_count = digit_count
	view.focused_digit = clamp(style.focused_digit, i32(0), max(i32(0), digit_count-1))
	style.focused_digit = view.focused_digit

	changed := !view.style_initialized || !numeric_time_style_equal(view.style, style) ||
		!numeric_time_segments_equal(view.segments, candidate)
	if changed {
		numeric_time_segments_destroy(view.segments, context.allocator)
		view.segments = candidate
		view.style = style
		view.style_initialized = true
		rt, status := alicorn.hosted_runtime_borrow(&view.hosted)
		if status != .Ok { return -1 }
		alicorn.invalidate_root(rt, "Audacity numeric time presentation changed")
	} else {
		numeric_time_segments_destroy(candidate, context.allocator)
	}
	return 1 if numeric_time_step(view).status == .Ok else -1
}

@(export, link_name="audacity_alicorn_numeric_time_view_key")
audacity_alicorn_numeric_time_view_key :: proc "c" (
	opaque_view: rawptr,
	key: u32,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Numeric_Time_View)opaque_view
	if !valid_numeric_time_view(view) {
		return -1
	}
	handled, status := alicorn.hosted_runtime_key(&view.hosted, alicorn.Hosted_Key_Event{
		key_code=key,
		pressed=true,
	})
	if status != .Ok {
		return -1
	}
	if handled && numeric_time_step(view).status != .Ok {
		return -1
	}
	return 1 if handled else 0
}

@(export, link_name="audacity_alicorn_numeric_time_view_focused_digit")
audacity_alicorn_numeric_time_view_focused_digit :: proc "c" (opaque_view: rawptr) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Numeric_Time_View)opaque_view
	if !valid_numeric_time_view(view) {
		return -1
	}
	return view.focused_digit
}

@(export, link_name="audacity_alicorn_numeric_time_view_paint")
audacity_alicorn_numeric_time_view_paint :: proc "c" (
	opaque_view: rawptr,
	callback: AudacitySnapDrawCallback,
	callback_context: rawptr,
) -> i32 {
	context = odin_runtime.default_context()
	view := transmute(^Numeric_Time_View)opaque_view
	if !valid_numeric_time_view(view) || callback == nil { return -1 }
	display, status := alicorn.hosted_runtime_display_commands(&view.hosted)
	if status != .Ok { return -1 }
	rt, borrow_status := alicorn.hosted_runtime_borrow(&view.hosted)
	if borrow_status != .Ok { return -1 }

	view.callback_active = true
	delivered := i32(0)
	for retained in display {
		output := AudacitySnapDrawCommand{}
		switch payload in retained.payload {
		case alicorn.Surface_Paint:
			output = snap_view_command_base(retained, AUDACITY_SNAP_DRAW_SURFACE)
			output.fill = snap_color(payload.fill)
			output.stroke = snap_color(payload.stroke.color)
			output.stroke_width = payload.stroke.width
			output.radius = payload.shape.corner_radius
			output.shape_kind = snap_shape_kind(payload.shape.kind)
			output.material_id = u32(payload.material)
		case alicorn.Icon_Paint:
			if payload.id != .Chevron_Down { continue }
			output = snap_view_command_base(retained, AUDACITY_SNAP_DRAW_ICON)
			output.icon_id = AUDACITY_SNAP_ICON_CHEVRON_DOWN
			output.fill = snap_color(numeric_time_color(view.style.menu_icon))
		case alicorn.Text_Paint:
			run, run_ok := alicorn.paint_text_run_resolve(rt, payload.run)
			if !run_ok { continue }
			output = snap_view_command_base(retained, AUDACITY_SNAP_DRAW_TEXT)
			output.text_color = snap_color(payload.color)
			output.font_size = run.size
			output.text_length = u64(len(run.value))
			if len(run.value) > 0 { output.text_utf8 = raw_data(run.value) }
		case alicorn.Backdrop_Effect_Paint, alicorn.Image_Paint, alicorn.Geometry_Paint:
			continue
		case:
			continue
		}
		callback(callback_context, &output)
		delivered += 1
	}
	view.callback_active = false
	if alicorn.hosted_runtime_frame_submission_succeeded(&view.hosted) != .Ok { return -1 }
	return delivered
}
