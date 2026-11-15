/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef AUDACITY_NUMERIC_TIME_VIEW_H
#define AUDACITY_NUMERIC_TIME_VIEW_H

#include <stdint.h>

#include "AudacitySnapView.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AudacityAlicornNumericTimeView AudacityAlicornNumericTimeView;

typedef struct AudacityNumericTimeSegment {
    const uint8_t *text_utf8;
    uint64_t text_length;
    float width;
    int32_t digit_index; /* -1 for formatter text, otherwise the digit ordinal. */
} AudacityNumericTimeSegment;

typedef struct AudacityNumericTimeStyle {
    AudacitySnapColor background;
    AudacitySnapColor digit_background;
    AudacitySnapColor focused_digit_background;
    AudacitySnapColor text_color;
    AudacitySnapColor focused_digit_text_color;
    AudacitySnapColor focus_border;
    AudacitySnapColor menu_icon;
    float width;
    float height;
    float content_width;
    float digit_height;
    float menu_width;
    float digit_font_size;
    float label_font_size;
    int32_t focused_digit;
    uint8_t focused;
    uint8_t enabled;
    uint8_t menu_enabled;
    uint8_t reserved;
} AudacityNumericTimeStyle;

AudacityAlicornNumericTimeView *audacity_alicorn_numeric_time_view_create(
    float width,
    float height,
    int32_t pixel_width,
    int32_t pixel_height,
    float dpi_scale);
void audacity_alicorn_numeric_time_view_destroy(
    AudacityAlicornNumericTimeView *view);
int32_t audacity_alicorn_numeric_time_view_resize(
    AudacityAlicornNumericTimeView *view,
    float width,
    float height,
    int32_t pixel_width,
    int32_t pixel_height,
    float dpi_scale);
int32_t audacity_alicorn_numeric_time_view_update(
    AudacityAlicornNumericTimeView *view,
    const AudacityNumericTimeSegment *segments,
    uint64_t segment_count,
    const AudacityNumericTimeStyle *style);
int32_t audacity_alicorn_numeric_time_view_paint(
    AudacityAlicornNumericTimeView *view,
    AudacitySnapDrawCallback callback,
    void *context);

#ifdef __cplusplus
}
#endif

#endif /* AUDACITY_NUMERIC_TIME_VIEW_H */
