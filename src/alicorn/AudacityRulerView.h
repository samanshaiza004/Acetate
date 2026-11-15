/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <stdint.h>

#include "AudacitySnapView.h"
#include "AudacityWaveformView.h"

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif
