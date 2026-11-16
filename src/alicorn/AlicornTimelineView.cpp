/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "AlicornTimelineView.h"

#include <algorithm>
#include <array>
#include <cmath>
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
   for (uint64_t i = 0; i < segmentCount; ++i) {
      const auto& segment = segments[i];
      const auto color = ToWxColour(AudacitySnapColor{
         segment.r, segment.g, segment.b, segment.a});
      const auto width = std::max(1, static_cast<int>(std::lround(segment.thickness)));
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

   if (command->kind == AUDACITY_SNAP_DRAW_TEXT &&
       context->labelIndex < context->labelFonts.size())
      context->command.font = context->labelFonts[context->labelIndex++];

   AudacityAlicorn::DrawPaintCommand(&context->command, command);
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
      if (mLastSnapshot && !UpdateRuler(*mLastSnapshot, true))
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

   std::unique_ptr<wxGraphicsContext> graphics{ wxGraphicsContext::Create(&dc) };
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
          mView, static_cast<uint32_t>(kind), &line) < 0)
      return false;
   mHasWaveformIndicators[indicatorIndex] = true;

   std::unique_ptr<wxGraphicsContext> graphics{ wxGraphicsContext::Create(&dc) };
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
          mView, static_cast<uint32_t>(kind), nullptr) < 0)
      return false;
   mHasWaveformIndicators[indicatorIndex] = false;

   const auto rect = clip.Intersect(wxRect{wxPoint{0, 0}, dc.GetSize()});
   if (rect.IsEmpty())
      return true;

   std::unique_ptr<wxGraphicsContext> graphics{ wxGraphicsContext::Create(&dc) };
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

   std::unique_ptr<wxGraphicsContext> graphics{ wxGraphicsContext::Create(&dc) };
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

bool AlicornTimelineView::UpdateRuler(
   const Ruler::DisplaySnapshot& snapshot, bool force)
{
   if (!mView)
      return false;
   if (!force && !mRulerNeedsUpdate && mLastSnapshot &&
       SameSnapshot(*mLastSnapshot, snapshot))
      return true;
   if (snapshot.ticks.size() > static_cast<size_t>(AUDACITY_RULER_MAX_TICKS) ||
       snapshot.labels.size() > 1024)
      return false;

   std::vector<AudacityRulerTick> ticks;
   ticks.reserve(snapshot.ticks.size());
   for (const auto& tick : snapshot.ticks) {
      ticks.push_back(AudacityRulerTick{
         static_cast<float>(tick.x1),
         static_cast<float>(tick.y1),
         static_cast<float>(tick.y2),
         1.0f,
         ToLinearColor(tick.colour)
      });
   }

   const auto orderedLabels = OrderedLabels(snapshot);
   std::vector<std::string> textStorage;
   textStorage.reserve(orderedLabels.size());
   for (const auto* label : orderedLabels) {
      const auto utf8 = label->text.utf8_str();
      if (!utf8.data())
         return false;
      textStorage.emplace_back(utf8.data(), utf8.length());
   }

   std::vector<AudacityRulerLabel> labels;
   labels.reserve(orderedLabels.size());
   for (size_t i = 0; i < orderedLabels.size(); ++i) {
      const auto& source = *orderedLabels[i];
      const auto& text = textStorage[i];
      const wxFont font{
         source.fontSize,
         wxFONTFAMILY_SWISS,
         wxFONTSTYLE_NORMAL,
         source.fontWeight
      };
      const auto pixelHeight = font.GetPixelSize().GetHeight();
      labels.push_back(AudacityRulerLabel{
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

   if (audacity_alicorn_timeline_view_update_ruler(
          mView,
          ticks.data(), static_cast<uint64_t>(ticks.size()),
          labels.data(), static_cast<uint64_t>(labels.size())) < 0)
      return false;

   mRulerTicks = std::move(ticks);
   mLastSnapshot = std::make_unique<Ruler::DisplaySnapshot>(snapshot);
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
       !UpdateRuler(snapshot, mRulerNeedsUpdate))
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

   std::unique_ptr<wxGraphicsContext> graphics{ wxGraphicsContext::Create(&dc) };
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
