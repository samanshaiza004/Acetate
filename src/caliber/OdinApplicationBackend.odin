package audacity_backend

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
