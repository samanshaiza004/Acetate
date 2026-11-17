/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "AlicornTimelineView.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <wx/graphics.h>

#include "toolbars/AlicornPaint.h"

namespace {
float SrgbToLinear(unsigned char value)
{
   const auto srgb = static_cast<float>(value) / 255.0f;
   return srgb <= 0.04045f ? srgb / 12.92f :
      std::pow((srgb + 0.055f) / 1.055f, 2.4f);
}

AudacitySnapColor ToLinearColor(const wxColour& color)
{
   return {
      SrgbToLinear(color.Red()),
      SrgbToLinear(color.Green()),
      SrgbToLinear(color.Blue()),
      static_cast<float>(color.Alpha()) / 255.0f
   };
}

double LinearToSrgb(float value)
{
   const auto linear = std::clamp(static_cast<double>(value), 0.0, 1.0);
   return linear <= 0.0031308
      ? linear * 12.92
      : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
}

wxColour ToWxColour(const AudacitySnapColor& color)
{
   return wxColour(
      static_cast<unsigned char>(std::lround(LinearToSrgb(color.r) * 255.0)),
      static_cast<unsigned char>(std::lround(LinearToSrgb(color.g) * 255.0)),
      static_cast<unsigned char>(std::lround(LinearToSrgb(color.b) * 255.0)),
      static_cast<unsigned char>(std::lround(std::clamp(color.a, 0.0f, 1.0f) * 255.0f)));
}

bool AppendRectangle(
   std::vector<AudacityWaveformRectangle>& rectangles,
   const wxRect& bounds,
   const wxColour& color)
{
   if (bounds.IsEmpty())
      return true;
   if (rectangles.size() >= AUDACITY_WAVEFORM_MAX_GEOMETRY_SEGMENTS)
      return false;
   const auto linear = ToLinearColor(color);
   rectangles.push_back({
      static_cast<float>(bounds.x),
      static_cast<float>(bounds.y),
      static_cast<float>(bounds.width),
      static_cast<float>(bounds.height),
      linear.r,
      linear.g,
      linear.b,
      linear.a,
   });
   return true;
}

bool AppendPointMarker(
   std::vector<AudacityWaveformPointMarker>& markers,
   float x,
   float y,
   float diameter,
   const wxColour& color)
{
   if (markers.size() >= AUDACITY_WAVEFORM_MAX_GEOMETRY_SEGMENTS)
      return false;
   const auto linear = ToLinearColor(color);
   markers.push_back({
      x, y, diameter, linear.r, linear.g, linear.b, linear.a
   });
   return true;
}

bool AppendRoundedRectangle(
   std::vector<AudacityWaveformRectangle>& rectangles,
   std::vector<AudacityWaveformPointMarker>& markers,
   const wxRect& bounds,
   int radius,
   const wxColour& color)
{
   if (bounds.IsEmpty())
      return true;
   radius = std::clamp(radius, 0, std::min(bounds.width, bounds.height) / 2);
   if (radius == 0)
      return AppendRectangle(rectangles, bounds, color);

   if (!AppendRectangle(
          rectangles,
          wxRect{bounds.x + radius, bounds.y,
                 bounds.width - radius * 2, bounds.height},
          color) ||
       !AppendRectangle(
          rectangles,
          wxRect{bounds.x, bounds.y + radius,
                 bounds.width, bounds.height - radius * 2},
          color))
      return false;

   const auto diameter = static_cast<float>(radius * 2);
   const auto left = static_cast<float>(bounds.x + radius);
   const auto right = static_cast<float>(bounds.GetRight() - radius + 1);
   const auto top = static_cast<float>(bounds.y + radius);
   const auto bottom = static_cast<float>(bounds.GetBottom() - radius + 1);
   return AppendPointMarker(markers, left, top, diameter, color) &&
      AppendPointMarker(markers, right, top, diameter, color) &&
      AppendPointMarker(markers, left, bottom, diameter, color) &&
      AppendPointMarker(markers, right, bottom, diameter, color);
}

struct PaintContext {
   wxGraphicsContext* graphics{};
   wxRect clip;
   int yOrigin{};
   wxPen pointMarkerPen;
   bool hasPointMarkerPen{};
   AudacityAlicorn::PaintContext command;
   std::vector<wxFont> labelFonts;
   size_t labelIndex{};
};

template<typename PaintProc>
bool PaintOutputAndAcknowledge(
   AudacityAlicornTimelineView* view,
   uint32_t outputKind,
   PaintProc&& paint)
{
   uint64_t revision{};
   const auto pending = audacity_alicorn_timeline_view_output_revision(
      view, outputKind, &revision);
   if (pending < 0 || paint() < 0)
      return false;
   return pending == 0 ||
      audacity_alicorn_timeline_view_output_submission_succeeded(
         view, outputKind, revision) > 0;
}

void DrawGeometry(
   void* rawContext,
   const AudacityWaveformRectangle* rectangles,
   uint64_t rectangleCount,
   const AudacityWaveformSegment* segments,
   uint64_t segmentCount,
   const AudacityWaveformPointMarker* pointMarkers,
   uint64_t pointMarkerCount,
   AudacitySnapRect bounds,
   AudacitySnapRect commandClip,
   float translationX,
   float translationY)
{
   auto* context = static_cast<PaintContext*>(rawContext);
   if (!context || !context->graphics ||
       (rectangleCount > 0 && !rectangles) || (segmentCount > 0 && !segments) ||
       (pointMarkerCount > 0 && !pointMarkers))
      return;

   bounds.y -= static_cast<float>(context->yOrigin);
   if (commandClip.width >= 0.0f && commandClip.height >= 0.0f)
      commandClip.y -= static_cast<float>(context->yOrigin);

   auto* graphics = context->graphics;
   graphics->PushState();
   graphics->Clip(
      context->clip.x, context->clip.y,
      context->clip.width, context->clip.height);
   if (commandClip.width >= 0.0f && commandClip.height >= 0.0f)
      graphics->Clip(
         commandClip.x, commandClip.y,
         commandClip.width, commandClip.height);
   graphics->Translate(bounds.x + translationX, bounds.y + translationY);

   graphics->SetPen(wxPen(wxColour(0, 0, 0, 0), 0, wxPENSTYLE_TRANSPARENT));
   wxColour previousFill;
   bool hasBrush = false;
   for (uint64_t i = 0; i < rectangleCount; ++i) {
      const auto& rectangle = rectangles[i];
      const auto color = ToWxColour(AudacitySnapColor{
         rectangle.r, rectangle.g, rectangle.b, rectangle.a});
      if (!hasBrush || color != previousFill) {
         graphics->SetBrush(wxBrush(color));
         previousFill = color;
         hasBrush = true;
      }
      graphics->DrawRectangle(
         rectangle.x, rectangle.y, rectangle.width, rectangle.height);
   }

   graphics->SetPen(context->hasPointMarkerPen
      ? context->pointMarkerPen
      : wxPen(wxColour(0, 0, 0, 0), 0, wxPENSTYLE_TRANSPARENT));
   wxColour previousMarkerFill;
   bool hasMarkerBrush = false;
   for (uint64_t i = 0; i < pointMarkerCount; ++i) {
      const auto& marker = pointMarkers[i];
      const auto color = ToWxColour(AudacitySnapColor{
         marker.r, marker.g, marker.b, marker.a});
      if (!hasMarkerBrush || color != previousMarkerFill) {
         graphics->SetBrush(wxBrush(color));
         previousMarkerFill = color;
         hasMarkerBrush = true;
      }
      const auto half = static_cast<double>(marker.diameter) * 0.5;
      graphics->DrawEllipse(
         marker.center_x - half,
         marker.center_y - half,
         marker.diameter,
         marker.diameter);
   }

   wxColour previousColour;
   int previousWidth = 0;
   bool hasPen = false;
   wxColour previousSegmentFill;
   bool hasSegmentFill = false;
   for (uint64_t i = 0; i < segmentCount; ++i) {
      const auto& segment = segments[i];
      const auto color = ToWxColour(AudacitySnapColor{
         segment.r, segment.g, segment.b, segment.a});
      const auto width = std::max(1, static_cast<int>(std::lround(segment.thickness)));
      if (segment.x0 == segment.x1 || segment.y0 == segment.y1) {
         // Stroking 1 pt vertical lines through wxGraphicsContext left the
         // first column of every separately submitted batch nearly blank (a
         // faint vertical seam in the waveform). Fill the segment's extent
         // instead: a vertical segment spans exactly y0..y1 and is `width`
         // wide centered on x; a horizontal one spans x0..x1 plus half a pen
         // width at each end because its endpoints are pixel centers.
         const auto half = static_cast<double>(width) * 0.5;
         const bool vertical = segment.x0 == segment.x1;
         const auto left = vertical
            ? segment.x0 - half
            : std::min(segment.x0, segment.x1) - half;
         const auto top = vertical
            ? std::min(segment.y0, segment.y1)
            : segment.y0 - half;
         const auto fillWidth = vertical
            ? static_cast<double>(width)
            : std::abs(segment.x1 - segment.x0) + width;
         const auto fillHeight = vertical
            ? std::abs(segment.y1 - segment.y0)
            : static_cast<double>(width);
         if (hasPen) {
            graphics->SetPen(wxPen(wxColour(0, 0, 0, 0), 0, wxPENSTYLE_TRANSPARENT));
            hasPen = false;
         }
         if (!hasSegmentFill || color != previousSegmentFill) {
            graphics->SetBrush(wxBrush(color));
            previousSegmentFill = color;
            hasSegmentFill = true;
         }
         graphics->DrawRectangle(left, top, fillWidth, fillHeight);
         continue;
      }
      if (!hasPen || color != previousColour || width != previousWidth) {
         graphics->SetPen(wxPen(color, width));
         previousColour = color;
         previousWidth = width;
         hasPen = true;
      }
      graphics->StrokeLine(segment.x0, segment.y0, segment.x1, segment.y1);
   }
   graphics->PopState();
}

void DrawCommand(void* rawContext, const AudacitySnapDrawCommand* command)
{
   auto* context = static_cast<PaintContext*>(rawContext);
   if (!context || !command)
      return;

   auto localCommand = *command;
   localCommand.bounds.y -= static_cast<float>(context->yOrigin);
   if (localCommand.clip.width >= 0.0f && localCommand.clip.height >= 0.0f)
      localCommand.clip.y -= static_cast<float>(context->yOrigin);

   if (localCommand.kind == AUDACITY_SNAP_DRAW_TEXT &&
       context->labelIndex < context->labelFonts.size())
      context->command.font = context->labelFonts[context->labelIndex++];

   AudacityAlicorn::DrawPaintCommand(&context->command, &localCommand);
}

bool SameSnapshot(
   const Ruler::DisplaySnapshot& left,
   const Ruler::DisplaySnapshot& right)
{
   if (left.ticks.size() != right.ticks.size() ||
       left.labels.size() != right.labels.size())
      return false;

   for (size_t i = 0; i < left.ticks.size(); ++i) {
      const auto& a = left.ticks[i];
      const auto& b = right.ticks[i];
      if (a.x1 != b.x1 || a.y1 != b.y1 || a.x2 != b.x2 || a.y2 != b.y2 ||
          a.colour != b.colour)
         return false;
   }
   for (size_t i = 0; i < left.labels.size(); ++i) {
      const auto& a = left.labels[i];
      const auto& b = right.labels[i];
      if (a.text != b.text || a.x != b.x || a.y != b.y || a.width != b.width ||
          a.height != b.height || a.fontSize != b.fontSize ||
          a.fontWeight != b.fontWeight || a.colour != b.colour)
         return false;
   }
   return true;
}

std::vector<const Ruler::DisplayLabel*> OrderedLabels(
   const Ruler::DisplaySnapshot& snapshot)
{
   std::vector<const Ruler::DisplayLabel*> labels;
   labels.reserve(snapshot.labels.size());
   for (const auto& label : snapshot.labels)
      labels.push_back(&label);
   std::stable_sort(labels.begin(), labels.end(), [](const auto* left, const auto* right) {
      if (left->x != right->x)
         return left->x < right->x;
      return left->y < right->y;
   });
   return labels;
}
}

AlicornTimelineView::AlicornTimelineView(
   const wxSize& rulerSize, const wxSize& waveformSize)
{
   Resize(rulerSize, waveformSize);
}

AlicornTimelineView::~AlicornTimelineView()
{
   audacity_alicorn_timeline_view_destroy(mView);
}

bool AlicornTimelineView::Resize(
   const wxSize& rulerSize, const wxSize& waveformSize)
{
   if (rulerSize.x <= 0 || rulerSize.y <= 0 ||
       waveformSize.x <= 0 || waveformSize.y <= 0)
      return false;

   if (!mView) {
      mView = audacity_alicorn_timeline_view_create(
         static_cast<float>(rulerSize.x),
         static_cast<float>(rulerSize.y),
         static_cast<float>(waveformSize.x),
         static_cast<float>(waveformSize.y));
      if (!mView)
         return false;
      mRulerSize = rulerSize;
      mWaveformSize = waveformSize;
      mRulerNeedsUpdate = true;
      return true;
   }

   if (mRulerSize == rulerSize && mWaveformSize == waveformSize)
      return true;

   const auto result = audacity_alicorn_timeline_view_resize(
      mView,
      static_cast<float>(rulerSize.x),
      static_cast<float>(rulerSize.y),
      static_cast<float>(waveformSize.x),
      static_cast<float>(waveformSize.y));
   if (result < 0)
      return false;

   mRulerSize = rulerSize;
   mWaveformSize = waveformSize;
   if (result > 0) {
      mRulerNeedsUpdate = true;
      if (!UpdateDescription(mLastSnapshot.get(), true))
         return false;
   }
   return true;
}

bool AlicornTimelineView::ResizeRuler(const wxSize& size)
{
   return Resize(size, mWaveformSize);
}

bool AlicornTimelineView::ResizeWaveform(const wxSize& size)
{
   return Resize(mRulerSize, size);
}

int32_t AlicornTimelineView::Pointer(
   uint32_t kind,
   float x,
   float y,
   uint32_t button,
   AudacityWaveformSelection& selection)
{
   if (!mView)
      return -1;
   selection = {};
   return audacity_alicorn_timeline_view_pointer(
      mView, kind, x, y, button, &selection);
}

int32_t AlicornTimelineView::ZoomPointer(
   uint32_t kind,
   float x,
   float y,
   uint32_t button,
   AudacityWaveformSelection& gesture)
{
   if (!mView)
      return -1;
   gesture = {};
   return audacity_alicorn_timeline_view_zoom_pointer(
      mView, kind, x, y, button, &gesture);
}

int32_t AlicornTimelineView::RulerPointer(
   uint32_t kind,
   float x,
   float y,
   uint32_t button,
   AudacityWaveformSelection& selection)
{
   if (!mView)
      return -1;
   selection = {};
   return audacity_alicorn_timeline_view_ruler_pointer(
      mView, kind, x, y, button, &selection);
}

bool AlicornTimelineView::Scroll(
   float x, float y, double steps, uint32_t modifiers)
{
   if (!mView || !std::isfinite(steps) ||
       steps < -1'000'000.0 || steps > 1'000'000.0)
      return false;
   return audacity_alicorn_timeline_view_scroll(
      mView, static_cast<float>(steps), x, y, modifiers) > 0;
}

bool AlicornTimelineView::DrawWaveform(
   wxDC& dc,
   const wxRect& clip,
   const AudacityWaveformRectangle* rectangles,
   size_t rectangleCount,
   const AudacityWaveformSegment* segments,
   size_t segmentCount,
   const AudacityWaveformPointMarker* pointMarkers,
   size_t pointMarkerCount,
   const wxPen* pointMarkerPen)
{
   if (!mView || !ResizeWaveform(dc.GetSize()) ||
       (rectangleCount > 0 && !rectangles) || (segmentCount > 0 && !segments) ||
       (pointMarkerCount > 0 && !pointMarkers))
      return false;
   if (rectangleCount == 0 && segmentCount == 0 && pointMarkerCount == 0)
      return true;

   if (audacity_alicorn_timeline_view_update_waveform(
          mView,
          rectangles, static_cast<uint64_t>(rectangleCount),
          segments, static_cast<uint64_t>(segmentCount),
          pointMarkers, static_cast<uint64_t>(pointMarkerCount)) < 0)
      return false;

   std::unique_ptr<wxGraphicsContext> graphics{ wxGraphicsContext::CreateFromUnknownDC(dc) };
   if (!graphics)
      return false;
   PaintContext context;
   context.graphics = graphics.get();
   context.clip = clip;
   context.yOrigin = mRulerSize.y;
   if (pointMarkerPen) {
      context.pointMarkerPen = *pointMarkerPen;
      context.hasPointMarkerPen = true;
   }
   return PaintOutputAndAcknowledge(
      mView,
      AUDACITY_TIMELINE_OUTPUT_WAVEFORM,
      [&] {
         return audacity_alicorn_timeline_view_paint_waveform(
            mView, &DrawGeometry, &context);
      });
}

bool AlicornTimelineView::DrawWaveformFills(
   wxDC& dc,
   const wxRect& clip,
   const WaveformFills& fills,
   size_t fillCount)
{
   if (fillCount > fills.size())
      return false;
   std::array<AudacityWaveformRectangle, 3> rectangles{};
   size_t rectangleCount{};
   for (size_t i = 0; i < fillCount; ++i) {
      const auto& fill = fills[i];
      if (fill.bounds.IsEmpty())
         continue;
      const auto linear = ToLinearColor(fill.color);
      rectangles[rectangleCount++] = AudacityWaveformRectangle{
         static_cast<float>(fill.bounds.x),
         static_cast<float>(fill.bounds.y),
         static_cast<float>(fill.bounds.width),
         static_cast<float>(fill.bounds.height),
         linear.r,
         linear.g,
         linear.b,
         linear.a
      };
   }
   if (rectangleCount == 0)
      return true;
   return DrawWaveform(
      dc, clip, rectangles.data(), rectangleCount, nullptr, 0, nullptr, 0);
}

bool AlicornTimelineView::DrawWaveformSegments(
   wxDC& dc,
   const wxRect& clip,
   const WaveformSegments& sourceSegments,
   size_t segmentCount)
{
   if (segmentCount > sourceSegments.size())
      return false;
   std::array<AudacityWaveformSegment, 4> segments{};
   for (size_t i = 0; i < segmentCount; ++i) {
      const auto& source = sourceSegments[i];
      const auto linear = ToLinearColor(source.color);
      segments[i] = AudacityWaveformSegment{
         static_cast<float>(source.start.x),
         static_cast<float>(source.start.y),
         static_cast<float>(source.end.x),
         static_cast<float>(source.end.y),
         source.thickness,
         linear.r,
         linear.g,
         linear.b,
         linear.a
      };
   }
   if (segmentCount == 0)
      return true;
   return DrawWaveform(
      dc, clip, nullptr, 0, segments.data(), segmentCount, nullptr, 0);
}

bool AlicornTimelineView::DrawWaveformLabels(
   wxDC& dc,
   const wxRect& clip,
   const std::vector<WaveformLabel>& labels)
{
   if (labels.size() > 1024 || !mView || !ResizeWaveform(dc.GetSize()))
      return false;
   const auto drawRect = clip.Intersect(wxRect{ wxPoint{0, 0}, dc.GetSize() });
   if (drawRect.IsEmpty())
      return true;

   auto orderedLabels = labels;
   std::stable_sort(
      orderedLabels.begin(), orderedLabels.end(),
      [](const auto& left, const auto& right) {
         if (left.bounds.x != right.bounds.x)
            return left.bounds.x < right.bounds.x;
         return left.bounds.y < right.bounds.y;
      });
   int64_t previousRight{};
   for (const auto& label : orderedLabels) {
      const auto right = static_cast<int64_t>(label.bounds.x) + label.bounds.width;
      const auto bottom = static_cast<int64_t>(label.bounds.y) + label.bounds.height;
      if (label.bounds.IsEmpty() || label.bounds.x < previousRight ||
          label.bounds.x < 0 || label.bounds.y < 0 ||
          right > dc.GetSize().x || bottom > dc.GetSize().y)
         return false;
      previousRight = right;
   }

   if (!SameWaveformLabels(orderedLabels)) {
      auto previous = std::move(mWaveformLabels);
      mWaveformLabels = orderedLabels;
      if (!UpdateDescription(mLastSnapshot.get(), true)) {
         mWaveformLabels = std::move(previous);
         return false;
      }
   }

   std::unique_ptr<wxGraphicsContext> graphics{ wxGraphicsContext::CreateFromUnknownDC(dc) };
   if (!graphics)
      return false;
   PaintContext context;
   context.graphics = graphics.get();
   context.clip = drawRect;
   context.yOrigin = mRulerSize.y;
   context.command.graphics = graphics.get();
   context.command.font = wxFont{};
   context.labelFonts.reserve(orderedLabels.size());
   for (const auto& label : orderedLabels)
      context.labelFonts.push_back(label.font);
   return PaintOutputAndAcknowledge(
      mView,
      AUDACITY_TIMELINE_OUTPUT_WAVEFORM_LABELS,
      [&] {
         const auto painted = audacity_alicorn_timeline_view_paint_waveform_labels(
            mView, &DrawCommand, &context);
         return painted == static_cast<int32_t>(orderedLabels.size()) ? 1 : -1;
      });
}

bool AlicornTimelineView::DrawClipAffordances(
   wxDC& dc,
   const wxRect& clip,
   const ClipAffordance* affordances,
   size_t affordanceCount)
{
   if (!mView || !ResizeWaveform(dc.GetSize()) ||
       (affordanceCount > 0 && !affordances))
      return false;

   auto& rectangles = mClipAffordanceRectangles;
   auto& markers = mClipAffordanceMarkers;
   rectangles.clear();
   markers.clear();
   rectangles.reserve(std::min<size_t>(
      affordanceCount * 2,
      AUDACITY_WAVEFORM_MAX_GEOMETRY_SEGMENTS));
   markers.reserve(std::min<size_t>(
      affordanceCount * 4,
      AUDACITY_WAVEFORM_MAX_GEOMETRY_SEGMENTS));

   for (size_t i = 0; i < affordanceCount; ++i) {
      const auto& affordance = affordances[i];
      if (!AppendRoundedRectangle(
             rectangles, markers, affordance.background,
             affordance.radius, affordance.fillColor))
         return false;
   }

   if (rectangles.size() + markers.size() >
       AUDACITY_WAVEFORM_MAX_GEOMETRY_SEGMENTS)
      return false;
   const auto* rectangleData = rectangles.empty() ? nullptr : rectangles.data();
   const auto* markerData = markers.empty() ? nullptr : markers.data();
   if (audacity_alicorn_timeline_view_update_clip_affordances(
          mView,
          rectangleData,
          static_cast<uint64_t>(rectangles.size()),
          markerData,
          static_cast<uint64_t>(markers.size())) < 0)
      return false;

   std::unique_ptr<wxGraphicsContext> graphics{ wxGraphicsContext::CreateFromUnknownDC(dc) };
   if (!graphics)
      return false;
   PaintContext context;
   context.graphics = graphics.get();
   context.clip = clip;
   context.yOrigin = mRulerSize.y;
   return PaintOutputAndAcknowledge(
      mView,
      AUDACITY_TIMELINE_OUTPUT_WAVEFORM,
      [&] {
         return audacity_alicorn_timeline_view_paint_clip_affordances(
            mView, &DrawGeometry, &context);
      });
}

bool AlicornTimelineView::DrawVerticalIndicator(
   wxDC& dc,
   const wxRect& clip,
   AudacityWaveformIndicatorKind kind,
   int x,
   const wxColour& color)
{
   if (!mView || !ResizeWaveform(dc.GetSize()))
      return false;

   const auto indicatorIndex = static_cast<size_t>(kind);
   if (indicatorIndex >= mHasWaveformIndicators.size())
      return false;

   const auto rect = clip.Intersect(wxRect{wxPoint{0, 0}, dc.GetSize()});
   if (rect.IsEmpty())
      return true;

   const auto linear = ToLinearColor(color);
   const AudacityWaveformSegment line{
      static_cast<float>(x),
      0.0f,
      static_cast<float>(x),
      static_cast<float>(dc.GetSize().GetHeight()),
      1.0f,
      linear.r,
      linear.g,
      linear.b,
      linear.a
   };
   if (audacity_alicorn_timeline_view_update_waveform_overlay(
          mView, static_cast<uint32_t>(kind), &line, 1) < 0)
      return false;
   mHasWaveformIndicators[indicatorIndex] = true;

   std::unique_ptr<wxGraphicsContext> graphics{ wxGraphicsContext::CreateFromUnknownDC(dc) };
   if (!graphics)
      return false;
   PaintContext context;
   context.graphics = graphics.get();
   context.clip = rect;
   context.yOrigin = mRulerSize.y;
   return PaintOutputAndAcknowledge(
      mView,
      AUDACITY_TIMELINE_OUTPUT_WAVEFORM,
      [&] {
         return audacity_alicorn_timeline_view_paint_waveform_overlay(
            mView, static_cast<uint32_t>(kind), &DrawGeometry, &context);
      });
}

bool AlicornTimelineView::DrawSnapGuides(
   wxDC& dc,
   const wxRect& clip,
   int firstX,
   int secondX,
   const wxColour& color)
{
   if (!mView || !ResizeWaveform(dc.GetSize()))
      return false;

   constexpr auto kind = AUDACITY_WAVEFORM_INDICATOR_SNAP_GUIDES;
   const auto indicatorIndex = static_cast<size_t>(kind);
   if (indicatorIndex >= mHasWaveformIndicators.size())
      return false;

   const auto rect = clip.Intersect(wxRect{wxPoint{0, 0}, dc.GetSize()});
   if (rect.IsEmpty())
      return true;

   std::array<AudacityWaveformSegment, 2> lines{};
   uint64_t count{};
   const auto linear = ToLinearColor(color);
   for (const auto x : { firstX, secondX }) {
      if (x < 0 || (count > 0 && x == firstX))
         continue;
      lines[count++] = AudacityWaveformSegment{
         static_cast<float>(x),
         0.0f,
         static_cast<float>(x),
         static_cast<float>(dc.GetSize().GetHeight()),
         1.0f,
         linear.r,
         linear.g,
         linear.b,
         linear.a
      };
   }

   if (count == 0 && !mHasWaveformIndicators[indicatorIndex])
      return true;
   if (audacity_alicorn_timeline_view_update_waveform_overlay(
          mView,
          static_cast<uint32_t>(kind),
          count > 0 ? lines.data() : nullptr,
          count) < 0)
      return false;
   if (count > 0)
      mHasWaveformIndicators[indicatorIndex] = true;

   std::unique_ptr<wxGraphicsContext> graphics{ wxGraphicsContext::CreateFromUnknownDC(dc) };
   if (!graphics)
      return false;
   PaintContext context;
   context.graphics = graphics.get();
   context.clip = rect;
   context.yOrigin = mRulerSize.y;
   const auto painted = PaintOutputAndAcknowledge(
      mView,
      AUDACITY_TIMELINE_OUTPUT_WAVEFORM,
      [&] {
         return audacity_alicorn_timeline_view_paint_waveform_overlay(
            mView, static_cast<uint32_t>(kind), &DrawGeometry, &context);
      });
   if (painted)
      mHasWaveformIndicators[indicatorIndex] = count > 0;
   return painted;
}

bool AlicornTimelineView::UpdateZoomPreview(
   bool active,
   int firstX,
   int secondX)
{
   if (!mView || mWaveformSize.y <= 0)
      return false;

   constexpr int dashLength = 4;
   constexpr int dashPeriod = 8;
   const auto height = mWaveformSize.y;
   const int64_t distance = std::abs(
      static_cast<int64_t>(secondX) - static_cast<int64_t>(firstX));
   const bool showPreview = active && distance > 3;
   std::array<AudacityWaveformSegment,
      AUDACITY_WAVEFORM_MAX_GEOMETRY_SEGMENTS> segments{};
   size_t count{};

   if (showPreview) {
      const auto dashesPerLine = static_cast<size_t>(
         (height + dashPeriod - 1) / dashPeriod);
      const auto lineCount = firstX == secondX ? size_t{1} : size_t{2};
      if (dashesPerLine > segments.size() / lineCount)
         return false;

      const auto black = ToLinearColor(wxColour(0, 0, 0));
      for (const auto x : { firstX, secondX }) {
         if (count > 0 && x == firstX)
            continue;
         for (int y = 0; y < height; y += dashPeriod) {
            const auto end = std::min(y + dashLength, height);
            segments[count++] = AudacityWaveformSegment{
               static_cast<float>(x),
               static_cast<float>(y),
               static_cast<float>(x),
               static_cast<float>(end),
               1.0f,
               black.r,
               black.g,
               black.b,
               black.a
            };
         }
      }
   }

   if (audacity_alicorn_timeline_view_update_waveform_overlay(
          mView,
          AUDACITY_WAVEFORM_INDICATOR_ZOOM_PREVIEW,
          count > 0 ? segments.data() : nullptr,
          static_cast<uint64_t>(count)) < 0)
      return false;

   mHasWaveformIndicators[
      AUDACITY_WAVEFORM_INDICATOR_ZOOM_PREVIEW] = count > 0;
   return true;
}

bool AlicornTimelineView::DrawZoomPreview(
   wxDC& dc,
   const wxRect& clip)
{
   constexpr auto kind = AUDACITY_WAVEFORM_INDICATOR_ZOOM_PREVIEW;
   if (!mView || !ResizeWaveform(dc.GetSize()))
      return false;
   if (!mHasWaveformIndicators[kind])
      return true;

   const auto rect = clip.Intersect(wxRect{ wxPoint{0, 0}, dc.GetSize() });
   if (rect.IsEmpty())
      return true;

   std::unique_ptr<wxGraphicsContext> graphics{ wxGraphicsContext::CreateFromUnknownDC(dc) };
   if (!graphics)
      return false;
   PaintContext context;
   context.graphics = graphics.get();
   context.clip = rect;
   context.yOrigin = mRulerSize.y;
   return PaintOutputAndAcknowledge(
      mView,
      AUDACITY_TIMELINE_OUTPUT_WAVEFORM,
      [&] {
         return audacity_alicorn_timeline_view_paint_waveform_overlay(
            mView, kind, &DrawGeometry, &context);
      });
}

bool AlicornTimelineView::ClearVerticalIndicator(
   wxDC& dc,
   const wxRect& clip,
   AudacityWaveformIndicatorKind kind)
{
   const auto indicatorIndex = static_cast<size_t>(kind);
   if (indicatorIndex >= mHasWaveformIndicators.size())
      return false;
   if (!mView || !mHasWaveformIndicators[indicatorIndex])
      return true;
   if (!ResizeWaveform(dc.GetSize()))
      return false;

   if (audacity_alicorn_timeline_view_update_waveform_overlay(
          mView, static_cast<uint32_t>(kind), nullptr, 0) < 0)
      return false;
   mHasWaveformIndicators[indicatorIndex] = false;

   const auto rect = clip.Intersect(wxRect{wxPoint{0, 0}, dc.GetSize()});
   if (rect.IsEmpty())
      return true;

   std::unique_ptr<wxGraphicsContext> graphics{ wxGraphicsContext::CreateFromUnknownDC(dc) };
   if (!graphics)
      return false;
   PaintContext context;
   context.graphics = graphics.get();
   context.clip = rect;
   context.yOrigin = mRulerSize.y;
   return PaintOutputAndAcknowledge(
      mView,
      AUDACITY_TIMELINE_OUTPUT_WAVEFORM,
      [&] {
         return audacity_alicorn_timeline_view_paint_waveform_overlay(
            mView, static_cast<uint32_t>(kind), &DrawGeometry, &context);
      });
}

bool AlicornTimelineView::DrawRulerIndicator(
   wxDC& dc, const wxRect& clip, int x, const wxColour& color)
{
   if (!mView || !ResizeRuler(dc.GetSize()))
      return false;

   const auto rect = clip.Intersect(wxRect{wxPoint{0, 0}, dc.GetSize()});
   if (rect.IsEmpty())
      return true;

   AudacityWaveformSegment line{};
   const AudacityWaveformSegment* linePointer{};
   if (x >= 0) {
      const auto linear = ToLinearColor(color);
      line = AudacityWaveformSegment{
         static_cast<float>(x),
         static_cast<float>(rect.GetTop()),
         static_cast<float>(x),
         static_cast<float>(rect.GetBottom() + 1),
         1.0f,
         linear.r,
         linear.g,
         linear.b,
         linear.a
      };
      linePointer = &line;
   }
   if (audacity_alicorn_timeline_view_update_ruler_overlay(
          mView, linePointer) < 0)
      return false;

   std::unique_ptr<wxGraphicsContext> graphics{ wxGraphicsContext::CreateFromUnknownDC(dc) };
   if (!graphics)
      return false;
   PaintContext context;
   context.graphics = graphics.get();
   context.clip = rect;
   return PaintOutputAndAcknowledge(
      mView,
      AUDACITY_TIMELINE_OUTPUT_RULER,
      [&] {
         return audacity_alicorn_timeline_view_paint_ruler_overlay(
            mView, &DrawGeometry, &context);
      });
}

bool AlicornTimelineView::SameWaveformLabels(
   const std::vector<WaveformLabel>& labels) const
{
   if (labels.size() != mWaveformLabels.size())
      return false;
   for (size_t i = 0; i < labels.size(); ++i) {
      const auto& left = labels[i];
      const auto& right = mWaveformLabels[i];
      if (left.bounds != right.bounds || left.text != right.text ||
          left.color != right.color ||
          left.font.GetPixelSize() != right.font.GetPixelSize() ||
          left.font.GetWeight() != right.font.GetWeight() ||
          left.font.GetStyle() != right.font.GetStyle() ||
          left.font.GetFaceName() != right.font.GetFaceName())
         return false;
   }
   return true;
}

bool AlicornTimelineView::UpdateDescription(
   const Ruler::DisplaySnapshot* snapshot, bool force)
{
   if (!mView)
      return false;
   if (!force && !mRulerNeedsUpdate &&
       ((!snapshot && !mLastSnapshot) ||
        (snapshot && mLastSnapshot && SameSnapshot(*mLastSnapshot, *snapshot))))
      return true;
   if ((snapshot && (snapshot->ticks.size() > static_cast<size_t>(AUDACITY_RULER_MAX_TICKS) ||
                     snapshot->labels.size() > 1024)) ||
       mWaveformLabels.size() > 1024)
      return false;

   std::vector<AudacityRulerTick> ticks;
   if (snapshot) {
      ticks.reserve(snapshot->ticks.size());
      for (const auto& tick : snapshot->ticks) {
         ticks.push_back(AudacityRulerTick{
            static_cast<float>(tick.x1),
            static_cast<float>(tick.y1),
            static_cast<float>(tick.y2),
            1.0f,
            ToLinearColor(tick.colour)
         });
      }
   }

   std::vector<const Ruler::DisplayLabel*> orderedLabels;
   if (snapshot)
      orderedLabels = OrderedLabels(*snapshot);
   std::vector<std::string> rulerTextStorage;
   rulerTextStorage.reserve(orderedLabels.size());
   for (const auto* label : orderedLabels) {
      const auto utf8 = label->text.utf8_str();
      if (!utf8.data())
         return false;
      rulerTextStorage.emplace_back(utf8.data(), utf8.length());
   }

   std::vector<AudacityTimelineLabel> rulerLabels;
   rulerLabels.reserve(orderedLabels.size());
   for (size_t i = 0; i < orderedLabels.size(); ++i) {
      const auto& source = *orderedLabels[i];
      const auto& text = rulerTextStorage[i];
      const wxFont font{
         source.fontSize,
         wxFONTFAMILY_SWISS,
         wxFONTSTYLE_NORMAL,
         source.fontWeight
      };
      const auto pixelHeight = font.GetPixelSize().GetHeight();
      rulerLabels.push_back(AudacityTimelineLabel{
         source.x,
         source.y,
         source.width,
         source.height,
         static_cast<float>(pixelHeight > 0 ? pixelHeight : source.height),
         static_cast<float>(source.fontWeight),
         ToLinearColor(source.colour),
         reinterpret_cast<const uint8_t*>(text.data()),
         static_cast<uint64_t>(text.size())
      });
   }

   std::vector<std::string> waveformTextStorage;
   waveformTextStorage.reserve(mWaveformLabels.size());
   for (const auto& label : mWaveformLabels) {
      const auto utf8 = label.text.utf8_str();
      if (!utf8.data())
         return false;
      waveformTextStorage.emplace_back(utf8.data(), utf8.length());
      if (waveformTextStorage.back().size() > 4096)
         return false;
   }

   std::vector<AudacityTimelineLabel> waveformLabels;
   waveformLabels.reserve(mWaveformLabels.size());
   for (size_t i = 0; i < mWaveformLabels.size(); ++i) {
      const auto& source = mWaveformLabels[i];
      const auto& text = waveformTextStorage[i];
      const auto pixelHeight = source.font.GetPixelSize().GetHeight();
      waveformLabels.push_back(AudacityTimelineLabel{
         source.bounds.x,
         source.bounds.y,
         source.bounds.width,
         source.bounds.height,
         static_cast<float>(pixelHeight > 0 ? pixelHeight : source.bounds.height),
         static_cast<float>(source.font.GetWeight()),
         ToLinearColor(source.color),
         reinterpret_cast<const uint8_t*>(text.data()),
         static_cast<uint64_t>(text.size())
      });
   }

   if (audacity_alicorn_timeline_view_update_description(
          mView,
          ticks.data(), static_cast<uint64_t>(ticks.size()),
          rulerLabels.data(), static_cast<uint64_t>(rulerLabels.size()),
          waveformLabels.data(), static_cast<uint64_t>(waveformLabels.size())) < 0)
      return false;

   mRulerTicks = std::move(ticks);
   if (snapshot)
      mLastSnapshot = std::make_unique<Ruler::DisplaySnapshot>(*snapshot);
   else
      mLastSnapshot.reset();
   mHasRulerFills = false;
   mRulerNeedsUpdate = false;
   return true;
}

bool AlicornTimelineView::DrawRuler(
   wxDC& dc,
   const wxRect& clip,
   const Ruler::DisplaySnapshot& snapshot,
   const RulerFill* fills,
   size_t fillCount)
{
   if (!mView || !ResizeRuler(dc.GetSize()) ||
       fillCount > AUDACITY_RULER_MAX_FILLS ||
       (fillCount > 0 && !fills) ||
       !UpdateDescription(&snapshot, mRulerNeedsUpdate))
      return false;

   std::array<AudacityWaveformRectangle, AUDACITY_RULER_MAX_FILLS> rectangles{};
   std::array<RulerFill, AUDACITY_RULER_MAX_FILLS> presentFills{};
   size_t rectangleCount{};
   for (size_t i = 0; i < fillCount; ++i) {
      const auto& fill = fills[i];
      if (fill.bounds.IsEmpty())
         continue;
      const auto color = ToLinearColor(fill.color);
      presentFills[rectangleCount] = fill;
      rectangles[rectangleCount++] = AudacityWaveformRectangle{
         static_cast<float>(fill.bounds.x),
         static_cast<float>(fill.bounds.y),
         static_cast<float>(fill.bounds.width),
         static_cast<float>(fill.bounds.height),
         color.r, color.g, color.b, color.a
      };
   }

   bool fillsChanged = !mHasRulerFills || rectangleCount != mLastRulerFillCount;
   for (size_t i = 0; !fillsChanged && i < rectangleCount; ++i)
      fillsChanged = presentFills[i].bounds != mLastRulerFills[i].bounds ||
         presentFills[i].color != mLastRulerFills[i].color;
   if (fillsChanged) {
      if (audacity_alicorn_timeline_view_update_ruler_geometry(
             mView,
             rectangles.data(), static_cast<uint64_t>(rectangleCount),
             mRulerTicks.data(), static_cast<uint64_t>(mRulerTicks.size())) < 0)
         return false;
      mLastRulerFills = presentFills;
      mLastRulerFillCount = rectangleCount;
      mHasRulerFills = true;
   }

   std::unique_ptr<wxGraphicsContext> graphics{ wxGraphicsContext::CreateFromUnknownDC(dc) };
   if (!graphics)
      return false;

   PaintContext context;
   context.graphics = graphics.get();
   context.clip = clip;
   context.command.graphics = graphics.get();
   context.command.font = wxFont{};
   const auto orderedLabels = OrderedLabels(snapshot);
   context.labelFonts.reserve(orderedLabels.size());
   for (const auto* label : orderedLabels) {
      context.labelFonts.emplace_back(
         label->fontSize,
         wxFONTFAMILY_SWISS,
         wxFONTSTYLE_NORMAL,
         label->fontWeight);
   }
   return PaintOutputAndAcknowledge(
      mView,
      AUDACITY_TIMELINE_OUTPUT_RULER,
      [&] {
         return audacity_alicorn_timeline_view_paint_ruler(
            mView, &DrawCommand, &DrawGeometry, &context);
      });
}
