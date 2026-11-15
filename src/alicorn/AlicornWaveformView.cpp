/*  SPDX-License-Identifier: GPL-2.0-or-later */
#include "AlicornWaveformView.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include <wx/graphics.h>

namespace {
struct PaintContext {
   wxGraphicsContext* graphics{};
   wxRect clip;
};

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

void DrawGeometry(
   void* rawContext,
   const AudacityWaveformSegment* segments,
   uint64_t segmentCount,
   AudacitySnapRect bounds,
   AudacitySnapRect commandClip,
   float translationX,
   float translationY)
{
   auto* context = static_cast<PaintContext*>(rawContext);
   if (!context || !context->graphics || !segments)
      return;

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
}

AlicornWaveformView::AlicornWaveformView(const wxSize& size)
{
   mView = audacity_alicorn_waveform_view_create(
      static_cast<float>(size.x), static_cast<float>(size.y));
}

AlicornWaveformView::~AlicornWaveformView()
{
   audacity_alicorn_waveform_view_destroy(mView);
}

bool AlicornWaveformView::Resize(const wxSize& size)
{
   if (!mView)
      return false;
   return audacity_alicorn_waveform_view_resize(
      mView, static_cast<float>(size.x), static_cast<float>(size.y)) >= 0;
}

int32_t AlicornWaveformView::Pointer(
   uint32_t kind,
   float x,
   float y,
   uint32_t button,
   AudacityWaveformSelection& selection)
{
   if (!mView)
      return -1;
   selection = {};
   return audacity_alicorn_waveform_view_pointer(
      mView, kind, x, y, button, &selection);
}

bool AlicornWaveformView::Draw(
   wxDC& dc,
   const wxRect& clip,
   const AudacityWaveformSegment* segments,
   size_t count)
{
   if (!mView || (count > 0 && !segments))
      return false;
   if (count == 0)
      return true;

   if (audacity_alicorn_waveform_view_update(
          mView, segments, static_cast<uint64_t>(count)) < 0)
      return false;

   std::unique_ptr<wxGraphicsContext> graphics{ wxGraphicsContext::Create(&dc) };
   if (!graphics)
      return false;
   PaintContext context{ graphics.get(), clip };
   return audacity_alicorn_waveform_view_paint(
      mView, &DrawGeometry, &context) >= 0;
}

bool AlicornWaveformView::DrawPlayhead(
   wxDC& dc, const wxRect& clip, int x, const wxColour& color)
{
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
   return Draw(dc, clip, &line, 1);
}
