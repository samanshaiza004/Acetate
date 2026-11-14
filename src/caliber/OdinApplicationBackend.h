/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns 0 for no SetSelected call, 1 to deselect, and 2 to select.
uint8_t audacity_odin_track_selection_action(
   int32_t mode, double first_track, double track_count, int32_t index);

#ifdef __cplusplus
}
#endif
