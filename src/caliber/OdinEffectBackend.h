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

#ifdef __cplusplus
}
#endif
