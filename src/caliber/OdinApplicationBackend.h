/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns 0 for no SetSelected call, 1 to deselect, and 2 to select.
uint8_t audacity_odin_track_selection_action(
   int32_t mode, double first_track, double track_count, int32_t index);

// Resolves the anchor and inclusive range for a Shift-click. An index of -1
// means that the corresponding anchor/selection endpoint is unavailable.
int32_t audacity_odin_shift_selection_range(
   int64_t track_count, int64_t clicked_index, int64_t stored_anchor_index,
   int64_t first_selected_index, int64_t last_selected_index,
   int64_t* out_anchor_index, int64_t* out_first_index,
   int64_t* out_last_index, uint8_t* out_has_anchor);

#ifdef __cplusplus
}
#endif
