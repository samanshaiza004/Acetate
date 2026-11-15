/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns 0 for no SetSelected call, 1 to deselect, and 2 to select.
uint8_t audacity_odin_track_selection_action(
   int32_t mode, double first_track, double track_count, int32_t index);

// Returns 0 for replacing selection, 1 for toggling one track, and 2 for
// extending from the retained anchor.
uint8_t audacity_odin_track_list_selection_action(
   uint8_t control_down, uint8_t shift_down, uint8_t has_anchor);

// Returns 0 to preserve the current selection, 1 to select all audio, and 2
// to reject the operation because the fallback selection is disallowed.
uint8_t audacity_odin_select_all_if_none_action(
   uint8_t selection_needed, uint8_t selection_allowed);

enum {
   AUDACITY_ODIN_EFFECT_GENERATE = 0,
   AUDACITY_ODIN_EFFECT_PROCESS = 1,
   AUDACITY_ODIN_EFFECT_ANALYZE = 2,
   AUDACITY_ODIN_EFFECT_TOOL = 3,
};

enum {
   AUDACITY_ODIN_EFFECT_PLAN_PUSH_HISTORY = 1u << 0,
   AUDACITY_ODIN_EFFECT_PLAN_REMEMBER = 1u << 1,
   AUDACITY_ODIN_EFFECT_PLAN_ZOOM_TO_FIT = 1u << 2,
   AUDACITY_ODIN_EFFECT_PLAN_SCROLL_TO_BOTTOM = 1u << 3,
};

// Computes post-effect application policy. C++ retains all project/UI effects.
int32_t audacity_odin_effect_completion_plan(
   int32_t effect_kind, uint8_t skip_history, uint8_t dont_repeat_last,
   uint8_t tracks_added, uint8_t had_tracks, uint8_t clean_tracks,
   double selection_start, uint32_t* out_plan);

// Returns whether this project may stop the process-wide audio stream.
int32_t audacity_odin_can_stop_audio_stream(
   uint8_t stream_active, uint8_t monitoring, uint8_t owned_by_project,
   uint8_t* out_can_stop);

// Chooses ordered candidate indices for existing-track recording. Returns an
// empty result when the candidates cannot satisfy the input-channel policy.
int32_t audacity_odin_choose_recording_tracks(
   const uint64_t* channel_counts, uint64_t candidate_count,
   uint64_t recording_channels, uint64_t* out_indices,
   uint64_t out_capacity, uint64_t* out_count);

enum {
   AUDACITY_ODIN_EMPTY_IMPORT_TEMPO_YES = 0,
   AUDACITY_ODIN_EMPTY_IMPORT_TEMPO_ASK = 1,
   AUDACITY_ODIN_EMPTY_IMPORT_TEMPO_NO = 2,
};

enum {
   AUDACITY_ODIN_EXISTING_IMPORT_TEMPO_MATCH = 0,
   AUDACITY_ODIN_EXISTING_IMPORT_TEMPO_MATCH_IF_BEATS = 1,
   AUDACITY_ODIN_EXISTING_IMPORT_TEMPO_DO_NOTHING = 2,
};

// Chooses whether imported music follows the tempo-detection import path.
int32_t audacity_odin_should_detect_import_tempo(
   uint8_t project_is_empty, int32_t empty_project_choice,
   int32_t existing_project_choice, uint8_t is_beats_and_measures,
   uint8_t* out_should_detect);

// Resolves the anchor and inclusive range for a Shift-click. An index of -1
// means that the corresponding anchor/selection endpoint is unavailable.
int32_t audacity_odin_shift_selection_range(
   int64_t track_count, int64_t clicked_index, int64_t stored_anchor_index,
   int64_t first_selected_index, int64_t last_selected_index,
   int64_t* out_anchor_index, int64_t* out_first_index,
   int64_t* out_last_index, uint8_t* out_has_anchor);

// Applies track/screen bounds and, for contraction, prevents selection edges
// from crossing. Returns zero on success; invalid values leave output intact.
int32_t audacity_odin_selection_adjustment_clamp(
   uint8_t is_contract, uint8_t moves_start, double t0, double t1,
   double requested_time, double end_limit, double* out_time);

#ifdef __cplusplus
}
#endif
