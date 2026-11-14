/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <caliber.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Resolves Select > At Time's relative offsets without retaining project data.
// The caller owns all values and output storage; the function is synchronous.
CaliberStatus audacity_odin_selection_time_calculate(
   int32_t relative_mode, double start_offset, double end_offset,
   double project_end, double selection_start, double selection_end,
   double* out_start, double* out_end);

#ifdef __cplusplus
}
#endif
