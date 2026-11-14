/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Processes one offline Fade block and advances the effect-owned sample index.
// The pointers are borrowed only for this synchronous call.
size_t audacity_odin_fade_process_block(
   uint8_t fade_in, int64_t total_samples, int64_t* sample_index,
   const float* input, float* output, size_t block_len);

// Accumulates an offline Normalize DC-analysis block without retaining it.
double audacity_odin_normalize_accumulate_sum(
   const float* input, size_t block_len, double initial_sum);

// Applies an offline Normalize block in place using the existing offset/gain.
void audacity_odin_normalize_process_block(
   float* samples, size_t block_len, float offset, float multiplier);

// Scores one channel's samples into the shared candidate positions in place.
void audacity_odin_zero_crossing_score_channel(
   float* samples, size_t sample_count, float* candidate_scores,
   size_t candidate_count);

// Returns nonzero when a candidate passes the existing quality threshold.
uint8_t audacity_odin_zero_crossing_best_offset(
   const float* candidate_scores, size_t candidate_count, int32_t track_count,
   int64_t* sample_offset);

#ifdef __cplusplus
}
#endif
