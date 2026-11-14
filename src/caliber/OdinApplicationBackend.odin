package audacity_backend

import "core:math"

// Returns 0 when the current track is untouched, 1 when C++ should deselect
// it, and 2 when C++ should select it. Track objects and mutation remain in
// Audacity; Odin owns only the Select Tracks range/mode decision.
@(export, link_name="audacity_odin_track_selection_action")
odin_track_selection_action :: proc "c" (
	mode: i32,
	first_track: f64,
	track_count: f64,
	index: i32,
) -> u8 {
	index_value := f64(index)
	selected := first_track <= index_value && index_value < first_track + track_count
	switch mode {
	case 0:
		if selected do return 2
		return 1
	case 1:
		if selected do return 2
	case 2:
		if selected do return 1
	}
	return 0
}

@(export, link_name="audacity_odin_track_list_selection_action")
odin_track_list_selection_action :: proc "c" (
	control_down: u8,
	shift_down: u8,
	has_anchor: u8,
) -> u8 {
	if control_down != 0 do return 1
	if shift_down != 0 && has_anchor != 0 do return 2
	return 0
}

@(export, link_name="audacity_odin_shift_selection_range")
odin_shift_selection_range :: proc "c" (
	track_count: i64,
	clicked_index: i64,
	stored_anchor_index: i64,
	first_selected_index: i64,
	last_selected_index: i64,
	out_anchor_index: ^i64,
	out_first_index: ^i64,
	out_last_index: ^i64,
	out_has_anchor: ^u8,
) -> i32 {
	if track_count <= 0 || clicked_index < 0 || clicked_index >= track_count ||
		out_anchor_index == nil || out_first_index == nil ||
		out_last_index == nil || out_has_anchor == nil {
		return i32(CALIBER_STATUS_INVALID_ARGUMENT)
	}

	anchor := stored_anchor_index
	if anchor < 0 || anchor >= track_count {
		anchor = -1
		if first_selected_index >= 0 && first_selected_index < track_count {
			last := last_selected_index
			if last < first_selected_index || last >= track_count {
				last = first_selected_index
			}
			if clicked_index >= first_selected_index {
				anchor = first_selected_index
			} else {
				anchor = last
			}
		}
	}

	if anchor >= 0 {
		out_anchor_index^ = anchor
		out_first_index^ = min(clicked_index, anchor)
		out_last_index^ = max(clicked_index, anchor)
		out_has_anchor^ = 1
	} else {
		out_anchor_index^ = -1
		out_first_index^ = clicked_index
		out_last_index^ = clicked_index
		out_has_anchor^ = 0
	}
	return i32(CALIBER_STATUS_OK)
}

@(export, link_name="audacity_odin_selection_adjustment_clamp")
odin_selection_adjustment_clamp :: proc "c" (
	is_contract: u8,
	moves_start: u8,
	t0: f64,
	t1: f64,
	requested_time: f64,
	end_limit: f64,
	out_time: ^f64,
) -> i32 {
	if is_contract > 1 || moves_start > 1 || out_time == nil ||
		math.is_nan(t0) || math.is_inf(t0) ||
		math.is_nan(t1) || math.is_inf(t1) ||
		math.is_nan(requested_time) || math.is_inf(requested_time) ||
		math.is_nan(end_limit) || math.is_inf(end_limit) {
		return i32(CALIBER_STATUS_INVALID_ARGUMENT)
	}

	new_time := max(0.0, requested_time)
	new_time = min(new_time, end_limit)
	if is_contract != 0 {
		if moves_start != 0 {
			new_time = min(t1, new_time)
		} else {
			new_time = max(t0, new_time)
		}
	}

	out_time^ = new_time
	return i32(CALIBER_STATUS_OK)
}
