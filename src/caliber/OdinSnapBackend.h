/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <caliber.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AudacityOdinSnapStateV1 {
   uint64_t project_id;
   uint32_t mode;
   uint32_t reserved;
   double selection_start;
   double selection_end;
   const uint8_t* snap_to_data;
   size_t snap_to_len;
} AudacityOdinSnapStateV1;

typedef CaliberStatus (*AudacityOdinSnapSetModeProc)(void*, uint32_t);
typedef CaliberStatus (*AudacityOdinSnapReadStateProc)(
   void*, AudacityOdinSnapStateV1*);

size_t audacity_odin_snap_backend_session_size(void);
size_t audacity_odin_snap_backend_api_prefix_size(void);
size_t audacity_odin_snap_backend_state_v1_size(void);
CaliberStatus audacity_odin_snap_backend_session_init(
   void* storage, size_t storage_size, uint64_t project_id,
   const CaliberApiV1* api);
void audacity_odin_snap_backend_session_deinit(void* storage);
CaliberStatus audacity_odin_snap_backend_dispatch(
   void* storage, uint32_t action, uint32_t value);
CaliberStatus audacity_odin_snap_backend_pump(
   void* storage, void* user_data, AudacityOdinSnapSetModeProc set_mode,
   AudacityOdinSnapReadStateProc read_state, uint8_t* apply_started);
CaliberStatus audacity_odin_snap_backend_read_latest(
   void* storage, AudacityOdinSnapStateV1* out_state, uint8_t* snap_to_buffer,
   size_t snap_to_capacity, size_t* snap_to_required, uint64_t* revision,
   uint8_t* has_state);
void audacity_odin_snap_backend_invalidate(void* storage, CaliberStatus status);

#ifdef __cplusplus
}
#endif
