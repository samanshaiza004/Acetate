/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <stdint.h>

#include "AudacityRulerView.h"
#include "AudacitySnapView.h"
#include "AudacityWaveformView.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AudacityAlicornTrackControlsView AudacityAlicornTrackControlsView;

typedef enum AudacityTrackControlsOutputKind {
   AUDACITY_TRACK_CONTROLS_OUTPUT_GEOMETRY = 0,
   AUDACITY_TRACK_CONTROLS_OUTPUT_LABELS = 1,
} AudacityTrackControlsOutputKind;

/* Presentation-only runtime for track-control panels. All coordinates are
 * track-panel local logical units. Audacity keeps hit testing and state. */
AudacityAlicornTrackControlsView* audacity_alicorn_track_controls_view_create(
   float width,
   float height);
void audacity_alicorn_track_controls_view_destroy(
   AudacityAlicornTrackControlsView* view);
int32_t audacity_alicorn_track_controls_view_resize(
   AudacityAlicornTrackControlsView* view,
   float width,
   float height);

/* Replaces the whole retained scene. Arrays are borrowed for the call and
 * colors are linear. Returns 1 on success, -1 on invalid input. */
int32_t audacity_alicorn_track_controls_view_update(
   AudacityAlicornTrackControlsView* view,
   const AudacityWaveformRectangle* rectangles,
   uint64_t rectangle_count,
   const AudacityWaveformSegment* segments,
   uint64_t segment_count,
   const AudacityWaveformPointMarker* point_markers,
   uint64_t point_marker_count,
   const AudacityTimelineLabel* labels,
   uint64_t label_count);

/* Paint callbacks are synchronous and may not reenter or destroy the view. */
int32_t audacity_alicorn_track_controls_view_paint_geometry(
   AudacityAlicornTrackControlsView* view,
   AudacityWaveformPaintCallback callback,
   void* context);
int32_t audacity_alicorn_track_controls_view_paint_labels(
   AudacityAlicornTrackControlsView* view,
   AudacitySnapDrawCallback callback,
   void* context);

/* Capture and acknowledge the exact revision for one output; same contract as
 * the timeline view. */
int32_t audacity_alicorn_track_controls_view_output_revision(
   AudacityAlicornTrackControlsView* view,
   uint32_t output_kind,
   uint64_t* revision);
int32_t audacity_alicorn_track_controls_view_output_submission_succeeded(
   AudacityAlicornTrackControlsView* view,
   uint32_t output_kind,
   uint64_t revision);

#ifdef __cplusplus
}
#endif
