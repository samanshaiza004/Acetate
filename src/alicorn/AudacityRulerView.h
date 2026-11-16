/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <stdint.h>

#include "AudacitySnapView.h"
#include "AudacityWaveformView.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
   AUDACITY_RULER_MAX_TICKS = 1365,
   AUDACITY_RULER_MAX_FILLS = 21
};

typedef struct AudacityRulerTick {
   float x;
   float y0;
   float y1;
   float thickness;
   AudacitySnapColor color;
} AudacityRulerTick;

typedef void (*AudacityRulerDrawCallback)(
   void* context,
   const AudacitySnapDrawCommand* command);

#ifdef __cplusplus
}
#endif
