/*  SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <stdint.h>

#include "AudacitySnapView.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AudacityAlicornWaveformView AudacityAlicornWaveformView;

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

typedef struct AudacityWaveformSelection {
    uint32_t phase;
    float anchor_x;
    float current_x;
} AudacityWaveformSelection;

enum {
    AUDACITY_WAVEFORM_SELECTION_NONE = 0,
    AUDACITY_WAVEFORM_SELECTION_BEGAN = 1,
    AUDACITY_WAVEFORM_SELECTION_UPDATED = 2,
    AUDACITY_WAVEFORM_SELECTION_ENDED = 3,
    AUDACITY_WAVEFORM_SELECTION_CANCELLED = 4
};

/*
 * Segment storage and paint placement are borrowed for the duration of the
 * callback. The callback must not reenter or destroy the view.
 */
typedef void (*AudacityWaveformPaintCallback)(
    void *context,
    const AudacityWaveformSegment *segments,
    uint64_t segment_count,
    AudacitySnapRect bounds,
    AudacitySnapRect clip,
    float translation_x,
    float translation_y);

/* Returns NULL if the runtime cannot be initialized. */
AudacityAlicornWaveformView *audacity_alicorn_waveform_view_create(
    float width,
    float height);
void audacity_alicorn_waveform_view_destroy(AudacityAlicornWaveformView *view);

/* resize: 1 changed, 0 unchanged, -1 invalid view/dimensions or busy view. */
int32_t audacity_alicorn_waveform_view_resize(
    AudacityAlicornWaveformView *view,
    float width,
    float height);

/* Pointer input is borrowed synchronously. Selection coordinates are panel-local. */
int32_t audacity_alicorn_waveform_view_pointer(
    AudacityAlicornWaveformView *view,
    uint32_t kind,
    float x,
    float y,
    uint32_t button,
    AudacityWaveformSelection *selection);

/*
 * Copies up to 1365 surface-local line segments into retained Alicorn geometry.
 * Segment coordinates are logical units. Colors are linear RGBA in [0, 1].
 * Returns 1 after the retained presentation is updated, or -1 on rejection.
 */
int32_t audacity_alicorn_waveform_view_update(
    AudacityAlicornWaveformView *view,
    const AudacityWaveformSegment *segments,
    uint64_t segment_count);

/*
 * Visits the current geometry payload synchronously. Returns the number of
 * geometry commands delivered, or -1 if the view or callback is invalid.
 */
int32_t audacity_alicorn_waveform_view_paint(
    AudacityAlicornWaveformView *view,
    AudacityWaveformPaintCallback callback,
    void *context);

#ifdef __cplusplus
}
#endif
