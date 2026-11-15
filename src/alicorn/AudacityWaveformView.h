/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <stdint.h>

#include "AudacitySnapView.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AudacityWaveformSegment {
   float x0;
   float y0;
   float x1;
   float y1;
   float thickness;
   float r;
   float g;
   float b;
   float a;
} AudacityWaveformSegment;

typedef struct AudacityWaveformRectangle {
   float x;
   float y;
   float width;
   float height;
   float r;
   float g;
   float b;
   float a;
} AudacityWaveformRectangle;

typedef struct AudacityWaveformPointMarker {
   float center_x;
   float center_y;
   float diameter;
   float r;
   float g;
   float b;
   float a;
} AudacityWaveformPointMarker;

typedef struct AudacityWaveformSelection {
   uint32_t phase;
   float anchor_x;
   float current_x;
} AudacityWaveformSelection;

enum {
   /* Alicorn's retained geometry surface accepts at most 8192 vertices. */
   AUDACITY_WAVEFORM_MAX_GEOMETRY_SEGMENTS = 8192 / 6,
   AUDACITY_WAVEFORM_SELECTION_NONE = 0,
   AUDACITY_WAVEFORM_SELECTION_BEGAN = 1,
   AUDACITY_WAVEFORM_SELECTION_UPDATED = 2,
   AUDACITY_WAVEFORM_SELECTION_ENDED = 3,
   AUDACITY_WAVEFORM_SELECTION_CANCELLED = 4
};

typedef void (*AudacityWaveformPaintCallback)(
   void *context,
   const AudacityWaveformRectangle *rectangles,
   uint64_t rectangle_count,
   const AudacityWaveformSegment *segments,
   uint64_t segment_count,
   const AudacityWaveformPointMarker *point_markers,
   uint64_t point_marker_count,
   AudacitySnapRect bounds,
   AudacitySnapRect clip,
   float translation_x,
   float translation_y);

#ifdef __cplusplus
}
#endif
