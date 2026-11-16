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

enum {
    AUDACITY_NUMERIC_TIME_KEY_LEFT = 1,
    AUDACITY_NUMERIC_TIME_KEY_RIGHT = 2,
    AUDACITY_NUMERIC_TIME_KEY_HOME = 3,
    AUDACITY_NUMERIC_TIME_KEY_END = 4,
    AUDACITY_NUMERIC_TIME_KEY_DIGIT_BASE = 16,
    AUDACITY_NUMERIC_TIME_KEY_BACKSPACE = 32,
    AUDACITY_NUMERIC_TIME_KEY_DELETE = 33,
    AUDACITY_NUMERIC_TIME_KEY_UP = 34,
    AUDACITY_NUMERIC_TIME_KEY_DOWN = 35
};

enum {
    AUDACITY_NUMERIC_TIME_KEY_RESULT_UNHANDLED = 0,
    AUDACITY_NUMERIC_TIME_KEY_RESULT_FOCUS_CHANGED = 1,
    AUDACITY_NUMERIC_TIME_KEY_RESULT_EDIT = 2
};

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
/* Route a native pointer transition through Alicorn hit testing. Returns 1
 * when a time digit was hit, 0 when no digit was hit, and -1 on invalid input. */
int32_t audacity_alicorn_numeric_time_view_pointer(
    AudacityAlicornNumericTimeView *view,
    uint32_t kind,
    float x,
    float y,
    uint32_t button);
/* Route numeric-time key behavior through the hosted Alicorn key callback.
 * Returns KEY_RESULT_FOCUS_CHANGED for navigation, KEY_RESULT_EDIT for a
 * value-edit key that the wx adapter applies through NumericTextCtrl,
 * KEY_RESULT_UNHANDLED for other keys, and -1 on an invalid view. Digit key
 * codes are DIGIT_BASE plus 0..9. */
int32_t audacity_alicorn_numeric_time_view_key(
    AudacityAlicornNumericTimeView *view,
    uint32_t key);
/* Returns the focused digit ordinal, or -1 for an invalid view. */
int32_t audacity_alicorn_numeric_time_view_focused_digit(
    AudacityAlicornNumericTimeView *view);
int32_t audacity_alicorn_numeric_time_view_paint(
    AudacityAlicornNumericTimeView *view,
    AudacitySnapDrawCallback callback,
    void *context);

#ifdef __cplusplus
}
#endif

#endif /* AUDACITY_NUMERIC_TIME_VIEW_H */
