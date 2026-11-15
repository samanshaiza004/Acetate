/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <stdint.h>

#include "AudacityRulerView.h"
#include "AudacityWaveformView.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AudacityAlicornTimelineView AudacityAlicornTimelineView;

/* One retained runtime contains both the ruler and track-panel regions. */
AudacityAlicornTimelineView* audacity_alicorn_timeline_view_create(
   float ruler_width,
   float ruler_height,
   float waveform_width,
   float waveform_height);
void audacity_alicorn_timeline_view_destroy(AudacityAlicornTimelineView* view);

/* All dimensions are logical wx client units. Resize is synchronous. */
int32_t audacity_alicorn_timeline_view_resize(
   AudacityAlicornTimelineView* view,
   float ruler_width,
   float ruler_height,
   float waveform_width,
   float waveform_height);

/* Pointer coordinates are track-panel local; the adapter maps them into the
 * waveform region of the shared timeline viewport. */
int32_t audacity_alicorn_timeline_view_pointer(
   AudacityAlicornTimelineView* view,
   uint32_t kind,
   float x,
   float y,
   uint32_t button,
   AudacityWaveformSelection* selection);

/* Geometry arrays are borrowed only for the call. Colors are linear. */
int32_t audacity_alicorn_timeline_view_update_waveform(
   AudacityAlicornTimelineView* view,
   const AudacityWaveformRectangle* rectangles,
   uint64_t rectangle_count,
   const AudacityWaveformSegment* segments,
   uint64_t segment_count);
int32_t audacity_alicorn_timeline_view_update_ruler(
   AudacityAlicornTimelineView* view,
   const AudacityRulerTick* ticks,
   uint64_t tick_count,
   const AudacityRulerLabel* labels,
   uint64_t label_count);

/* Paint callbacks are synchronous and may not reenter or destroy the view.
 * Waveform bounds/clip are translated to track-panel local coordinates;
 * ruler commands remain ruler-panel local. */
int32_t audacity_alicorn_timeline_view_paint_waveform(
   AudacityAlicornTimelineView* view,
   AudacityWaveformPaintCallback callback,
   void* context);
int32_t audacity_alicorn_timeline_view_paint_ruler(
   AudacityAlicornTimelineView* view,
   AudacityRulerDrawCallback draw_callback,
   AudacityWaveformPaintCallback geometry_callback,
   void* context);

#ifdef __cplusplus
}
#endif
