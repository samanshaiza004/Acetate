/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <cstddef>
#include <array>
#include <memory>
#include <vector>

#include <wx/dc.h>
#include <wx/gdicmn.h>
#include <wx/colour.h>
#include <wx/font.h>
#include <wx/pen.h>

#include "AudacityTimelineView.h"
#include "AudacityTrackControlsView.h"
#include "widgets/Ruler.h"

// A retained presentation scene in the local coordinates of one wx window.
// Audacity's windows keep hit testing and state; the scene only carries
// presentation. Rectangles paint first (in push order), then point markers,
// then line segments.
struct AlicornSceneLabel {
   wxRect bounds;
   wxString text;
   wxColour color;
   wxFont font;
};

struct AlicornScene {
   std::vector<AudacityWaveformRectangle> rectangles;
   std::vector<AudacityWaveformSegment> segments;
   std::vector<AudacityWaveformPointMarker> pointMarkers;
   std::vector<AlicornSceneLabel> labels;

   void Fill(const wxRect& bounds, const wxColour& color);
   void Line(
      const wxPoint& start, const wxPoint& end, const wxColour& color,
      float thickness = 1.0f);
   void Dot(float centerX, float centerY, float diameter, const wxColour& color);
   void Text(
      const wxRect& bounds, const wxString& text, const wxColour& color,
      const wxFont& font);
   bool Empty() const noexcept
   {
      return rectangles.empty() && segments.empty() &&
         pointMarkers.empty() && labels.empty();
   }
};

// A presentation-only retained runtime for one wx window.
class AlicornSceneView final {
public:
   AlicornSceneView() = default;
   ~AlicornSceneView();
   AlicornSceneView(const AlicornSceneView&) = delete;
   AlicornSceneView& operator=(const AlicornSceneView&) = delete;

   // Replaces the scene and paints it. Returns false when the retained path is
   // unavailable so the caller can draw with wx instead.
   bool Draw(wxDC& dc, const wxRect& clip, const AlicornScene& scene);

private:
   AudacityAlicornTrackControlsView* mView{};
   wxSize mSize;
};

class AlicornTimelineView final {
public:
   struct RulerFill {
      wxRect bounds;
      wxColour color;
   };
   struct WaveformFill {
      wxRect bounds;
      wxColour color;
   };
   struct WaveformSegment {
      wxPoint start;
      wxPoint end;
      wxColour color;
      float thickness{ 1.0f };
   };
   struct WaveformLabel {
      wxRect bounds;
      wxString text;
      wxColour color;
      wxFont font;
   };
   struct ClipAffordance {
      wxRect background;
      int radius{};
      wxColour fillColor;
   };
   using TrackControlsScene = AlicornScene;
   using WaveformFills = std::array<WaveformFill, 3>;
   using WaveformSegments = std::array<WaveformSegment, 4>;

   AlicornTimelineView() = default;
   explicit AlicornTimelineView(const wxSize& rulerSize, const wxSize& waveformSize);
   ~AlicornTimelineView();

   AlicornTimelineView(const AlicornTimelineView&) = delete;
   AlicornTimelineView& operator=(const AlicornTimelineView&) = delete;

   bool IsReady() const noexcept { return mView != nullptr; }
   bool Resize(const wxSize& rulerSize, const wxSize& waveformSize);
   bool ResizeRuler(const wxSize& size);
   bool ResizeWaveform(const wxSize& size);

   int32_t Pointer(
      uint32_t kind,
      float x,
      float y,
      uint32_t button,
      AudacityWaveformSelection& selection);
   int32_t ZoomPointer(
      uint32_t kind,
      float x,
      float y,
      uint32_t button,
      AudacityWaveformSelection& gesture);
   int32_t RulerPointer(
      uint32_t kind,
      float x,
      float y,
      uint32_t button,
      AudacityWaveformSelection& selection);
   bool Scroll(float x, float y, double steps, uint32_t modifiers);
   bool DrawWaveform(
      wxDC& dc,
      const wxRect& clip,
      const AudacityWaveformRectangle* rectangles,
      size_t rectangleCount,
      const AudacityWaveformSegment* segments,
      size_t segmentCount,
      const AudacityWaveformPointMarker* pointMarkers,
      size_t pointMarkerCount,
      const wxPen* pointMarkerPen = nullptr);
   bool DrawWaveformFills(
      wxDC& dc,
      const wxRect& clip,
      const WaveformFills& fills,
      size_t fillCount);
   bool DrawWaveformSegments(
      wxDC& dc,
      const wxRect& clip,
      const WaveformSegments& segments,
      size_t segmentCount);
   bool DrawWaveformLabels(
      wxDC& dc,
      const wxRect& clip,
      const std::vector<WaveformLabel>& labels);
   bool DrawClipAffordances(
      wxDC& dc,
      const wxRect& clip,
      const ClipAffordance* affordances,
      size_t affordanceCount);
   bool DrawVerticalIndicator(
      wxDC& dc,
      const wxRect& clip,
      AudacityWaveformIndicatorKind kind,
      int x,
      const wxColour& color);
   bool DrawSnapGuides(
      wxDC& dc,
      const wxRect& clip,
      int firstX,
      int secondX,
      const wxColour& color);
   // Paints one track-control panel scene. Returns false when the retained
   // path is unavailable so the caller can draw with wx instead.
   bool DrawTrackControls(
      wxDC& dc, const wxRect& clip, const TrackControlsScene& scene);
   bool UpdateZoomPreview(bool active, int firstX, int secondX);
   bool DrawZoomPreview(wxDC& dc, const wxRect& clip);
   bool ClearVerticalIndicator(
      wxDC& dc,
      const wxRect& clip,
      AudacityWaveformIndicatorKind kind);
   bool DrawRulerIndicator(
      wxDC& dc,
      const wxRect& clip,
      int x,
      const wxColour& color);
   bool DrawRuler(
      wxDC& dc,
      const wxRect& clip,
      const Ruler::DisplaySnapshot& snapshot,
      const RulerFill* fills = nullptr,
      size_t fillCount = 0);

private:
   bool UpdateDescription(const Ruler::DisplaySnapshot* snapshot, bool force = false);
   bool SameWaveformLabels(const std::vector<WaveformLabel>& labels) const;

   AudacityAlicornTimelineView* mView{};
   AlicornSceneView mControls;
   wxSize mRulerSize;
   wxSize mWaveformSize;
   std::unique_ptr<Ruler::DisplaySnapshot> mLastSnapshot;
   std::vector<AudacityRulerTick> mRulerTicks;
   std::vector<WaveformLabel> mWaveformLabels;
   std::vector<AudacityWaveformRectangle> mClipAffordanceRectangles;
   std::vector<AudacityWaveformPointMarker> mClipAffordanceMarkers;
   std::array<RulerFill, AUDACITY_RULER_MAX_FILLS> mLastRulerFills{};
   size_t mLastRulerFillCount{};
   bool mHasRulerFills{};
   std::array<bool, AUDACITY_WAVEFORM_INDICATOR_COUNT> mHasWaveformIndicators{};
   bool mRulerNeedsUpdate{ true };
};
