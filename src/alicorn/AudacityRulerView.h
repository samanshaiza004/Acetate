/*  SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <stdint.h>

#include "AudacitySnapView.h"
#include "AudacityWaveformView.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AudacityAlicornRulerView AudacityAlicornRulerView;

enum { AUDACITY_RULER_MAX_TICKS = 1365 };

typedef struct AudacityRulerTick {
   float x;
   float y0;
   float y1;
   float thickness;
   AudacitySnapColor color;
} AudacityRulerTick;

typedef struct AudacityRulerLabel {
   int32_t x;
   int32_t y;
   int32_t width;
   int32_t height;
   float font_size;
   float font_weight;
   AudacitySnapColor color;
   const uint8_t* text_utf8;
   uint64_t text_length;
} AudacityRulerLabel;

typedef void (*AudacityRulerDrawCallback)(
   void* context,
   const AudacitySnapDrawCommand* command);

/* Call all operations on the creating thread. Text and tick arrays are
 * borrowed only for update; Alicorn copies accepted display data. */
AudacityAlicornRulerView* audacity_alicorn_ruler_view_create(
   float width,
   float height);
void audacity_alicorn_ruler_view_destroy(AudacityAlicornRulerView* view);
int32_t audacity_alicorn_ruler_view_resize(
   AudacityAlicornRulerView* view,
   float width,
   float height);
int32_t audacity_alicorn_ruler_view_update(
   AudacityAlicornRulerView* view,
   const AudacityRulerTick* ticks,
   uint64_t tick_count,
   const AudacityRulerLabel* labels,
   uint64_t label_count);
int32_t audacity_alicorn_ruler_view_paint(
   AudacityAlicornRulerView* view,
   AudacityRulerDrawCallback draw_callback,
   AudacityWaveformPaintCallback geometry_callback,
   void* context);

#ifdef __cplusplus
}
#endif
