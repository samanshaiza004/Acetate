/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <stdint.h>

#include "AudacityRulerView.h"
#include "AudacityWaveformView.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AudacityAlicornTimelineView AudacityAlicornTimelineView;

typedef enum AudacityWaveformIndicatorKind {
   AUDACITY_WAVEFORM_INDICATOR_CARET = 0,
   AUDACITY_WAVEFORM_INDICATOR_PLAYHEAD = 1,
   AUDACITY_WAVEFORM_INDICATOR_SNAP_GUIDES = 2,
   AUDACITY_WAVEFORM_INDICATOR_ZOOM_PREVIEW = 3,
   AUDACITY_WAVEFORM_INDICATOR_COUNT = 4,
} AudacityWaveformIndicatorKind;

typedef enum AudacityTimelineOutputKind {
   AUDACITY_TIMELINE_OUTPUT_RULER = 0,
   AUDACITY_TIMELINE_OUTPUT_WAVEFORM = 1,
   AUDACITY_TIMELINE_OUTPUT_WAVEFORM_LABELS = 2,
} AudacityTimelineOutputKind;

/* One retained runtime contains ruler, waveform, and waveform-label outputs. */
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

/* Zoom-tool pointer coordinates are track-panel local. Alicorn tracks the
 * gesture only; Audacity remains responsible for zoom and scroll policy. */
int32_t audacity_alicorn_timeline_view_zoom_pointer(
   AudacityAlicornTimelineView* view,
   uint32_t kind,
   float x,
   float y,
   uint32_t button,
   AudacityWaveformSelection* gesture);

/* Pointer coordinates are ruler-panel local and track only an Alicorn-owned
 * drag gesture; Audacity remains responsible for ruler actions and state. */
int32_t audacity_alicorn_timeline_view_ruler_pointer(
   AudacityAlicornTimelineView* view,
   uint32_t kind,
   float x,
   float y,
   uint32_t button,
   AudacityWaveformSelection* selection);

/* Scroll input is hit-tested against Alicorn's retained waveform region.
 * Positive step counts indicate wheel-up. The app applies Audacity's
 * project-specific zoom and scroll policy. Modifiers are Shift=1, Control=2,
 * Alt=4, Super=8. Returns 1 when captured, 0 outside the region, and -1 on
 * invalid input. */
int32_t audacity_alicorn_timeline_view_scroll(
   AudacityAlicornTimelineView* view,
   float steps,
   float x,
   float y,
   uint32_t modifiers);

/* Geometry arrays are borrowed only for the call. Colors are linear. */
int32_t audacity_alicorn_timeline_view_update_waveform(
   AudacityAlicornTimelineView* view,
   const AudacityWaveformRectangle* rectangles,
   uint64_t rectangle_count,
   const AudacityWaveformSegment* segments,
   uint64_t segment_count,
   const AudacityWaveformPointMarker* point_markers,
   uint64_t point_marker_count);
/* Clip affordances have a separate retained geometry owner so their update
 * cannot replace waveform or selection geometry. */
int32_t audacity_alicorn_timeline_view_update_clip_affordances(
   AudacityAlicornTimelineView* view,
   const AudacityWaveformRectangle* rectangles,
   uint64_t rectangle_count,
   const AudacityWaveformPointMarker* point_markers,
   uint64_t point_marker_count);
/* Transient indicators have independent retained owners. Snap guides accept
 * up to two line segments; zoom preview accepts a bounded geometry batch;
 * caret and playhead accept at most one. */
int32_t audacity_alicorn_timeline_view_update_waveform_overlay(
   AudacityAlicornTimelineView* view,
   uint32_t indicator_kind,
   const AudacityWaveformSegment* segments,
   uint64_t segment_count);
int32_t audacity_alicorn_timeline_view_update_description(
   AudacityAlicornTimelineView* view,
   const AudacityRulerTick* ticks,
   uint64_t tick_count,
   const AudacityTimelineLabel* ruler_labels,
   uint64_t ruler_label_count,
   const AudacityTimelineLabel* waveform_labels,
   uint64_t waveform_label_count);

/* Paints retained text labels in waveform-local coordinates. */
int32_t audacity_alicorn_timeline_view_paint_waveform_labels(
   AudacityAlicornTimelineView* view,
   AudacitySnapDrawCallback callback,
   void* context);

/* Presentation-only update for ruler fills and ticks. This does not rebuild
 * the retained description, so selection drags can update without layout. */
int32_t audacity_alicorn_timeline_view_update_ruler_geometry(
   AudacityAlicornTimelineView* view,
   const AudacityWaveformRectangle* rectangles,
   uint64_t rectangle_count,
   const AudacityRulerTick* ticks,
   uint64_t tick_count);

/* Presentation-only indicator drawn above ruler labels; null clears it. */
int32_t audacity_alicorn_timeline_view_update_ruler_overlay(
   AudacityAlicornTimelineView* view,
   const AudacityWaveformSegment* segment);

/* Paint callbacks are synchronous and may not reenter or destroy the view.
 * Waveform geometry and label bounds/clip are translated to track-panel local
 * coordinates; ruler commands remain ruler-panel local. */
int32_t audacity_alicorn_timeline_view_paint_waveform(
   AudacityAlicornTimelineView* view,
   AudacityWaveformPaintCallback callback,
   void* context);
int32_t audacity_alicorn_timeline_view_paint_clip_affordances(
   AudacityAlicornTimelineView* view,
   AudacityWaveformPaintCallback callback,
   void* context);
int32_t audacity_alicorn_timeline_view_paint_waveform_overlay(
   AudacityAlicornTimelineView* view,
   uint32_t indicator_kind,
   AudacityWaveformPaintCallback callback,
   void* context);
/* Base ruler paint excludes the transient overlay geometry below. */
int32_t audacity_alicorn_timeline_view_paint_ruler(
   AudacityAlicornTimelineView* view,
   AudacityRulerDrawCallback draw_callback,
   AudacityWaveformPaintCallback geometry_callback,
   void* context);
int32_t audacity_alicorn_timeline_view_paint_ruler_overlay(
   AudacityAlicornTimelineView* view,
   AudacityWaveformPaintCallback geometry_callback,
   void* context);

/* Capture and acknowledge the exact revision for one independently painted
 * target. Capture returns 1 with a revision, 0 when clean, and -1 on error.
 * Acknowledge only after the target's synchronous paint succeeds. */
int32_t audacity_alicorn_timeline_view_output_revision(
   AudacityAlicornTimelineView* view,
   uint32_t output_kind,
   uint64_t* revision);
int32_t audacity_alicorn_timeline_view_output_submission_succeeded(
   AudacityAlicornTimelineView* view,
   uint32_t output_kind,
   uint64_t revision);

#ifdef __cplusplus
}
#endif
