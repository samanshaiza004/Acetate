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

// Processes one Echo block with the effect-owned delay ring and position.
// Buffers are borrowed only for this synchronous call; C++ owns allocation.
int32_t audacity_odin_echo_process_block(
   const float* input, float* output, float* history, uint64_t history_len,
   uint64_t* history_position, uint64_t block_len, double decay);

// Computes compressor gain reduction. State belongs to the caller and is
// carried across blocks; exact input/output aliasing is supported. No
// allocation, locking, or Caliber transport occurs.
void audacity_odin_compressor_process_envelope(
   const float* sidechain, float* destination, size_t block_len,
   float threshold_db, float knee_db, float slope, float alpha_attack,
   float alpha_release, float* state, float* max_input_db,
   float* max_gain_reduction_db);

// Shapes the gain-reduction delay ring synchronously, without allocation.
void audacity_odin_compressor_lookahead_process(
   float* buffer, int32_t buffer_size, int32_t write_position,
   int32_t last_pushed_samples, int32_t delay_in_samples);

// Applies the current compressor envelope and returns the channel peak.
void audacity_odin_compressor_apply_envelope(
   const float* delayed_input, float* output, const float* envelope,
   size_t block_len, float makeup_gain_db, float* abs_max,
   int32_t* abs_max_index);

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
