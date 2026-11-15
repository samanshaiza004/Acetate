/*  SPDX-License-Identifier: GPL-2.0-or-later */
#include "AlicornRulerView.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
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

struct PaintContext {
   wxGraphicsContext* graphics{};
   wxRect clip;
   AudacityAlicorn::PaintContext command;
   std::vector<wxFont> labelFonts;
   size_t labelIndex{};
};

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

bool SameColor(const wxColour& left, const wxColour& right)
{
   return left == right;
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
          !SameColor(a.colour, b.colour))
         return false;
   }
   for (size_t i = 0; i < left.labels.size(); ++i) {
      const auto& a = left.labels[i];
      const auto& b = right.labels[i];
      if (a.text != b.text || a.x != b.x || a.y != b.y || a.width != b.width ||
          a.height != b.height || a.fontSize != b.fontSize ||
          a.fontWeight != b.fontWeight || !SameColor(a.colour, b.colour))
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

AlicornRulerView::AlicornRulerView(const wxSize& size)
{
   mView = audacity_alicorn_ruler_view_create(
      static_cast<float>(size.x), static_cast<float>(size.y));
}

AlicornRulerView::~AlicornRulerView()
{
   audacity_alicorn_ruler_view_destroy(mView);
}

bool AlicornRulerView::Resize(const wxSize& size)
{
   if (!mView)
      return false;
   const auto result = audacity_alicorn_ruler_view_resize(
      mView, static_cast<float>(size.x), static_cast<float>(size.y));
   if (result < 0)
      return false;
   if (result > 0)
      mLastSnapshot.reset();
   return true;
}

bool AlicornRulerView::Update(const Ruler::DisplaySnapshot& snapshot)
{
   if (!mView)
      return false;
   if (mLastSnapshot && SameSnapshot(*mLastSnapshot, snapshot))
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
   std::vector<wxFont> labelFonts;
   labelFonts.reserve(orderedLabels.size());
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
      labelFonts.push_back(font);
   }

   if (audacity_alicorn_ruler_view_update(
          mView,
          ticks.data(), static_cast<uint64_t>(ticks.size()),
          labels.data(), static_cast<uint64_t>(labels.size())) < 0)
      return false;

   mLastSnapshot = std::make_unique<Ruler::DisplaySnapshot>(snapshot);
   return true;
}

bool AlicornRulerView::Draw(
   wxDC& dc,
   const wxRect& clip,
   const Ruler::DisplaySnapshot& snapshot)
{
   if (!mView || !Resize(dc.GetSize()) || !Update(snapshot))
      return false;

   std::unique_ptr<wxGraphicsContext> graphics{ wxGraphicsContext::Create(&dc) };
   if (!graphics)
      return false;

   PaintContext context;
   context.graphics = graphics.get();
   context.clip = clip;
   const auto orderedLabels = OrderedLabels(snapshot);
   context.labelFonts.reserve(orderedLabels.size());
   for (const auto* label : orderedLabels) {
      context.labelFonts.emplace_back(
         label->fontSize,
         wxFONTFAMILY_SWISS,
         wxFONTSTYLE_NORMAL,
         label->fontWeight);
   }
   context.command.graphics = graphics.get();
   context.command.font = wxFont{};

   return audacity_alicorn_ruler_view_paint(
      mView, &DrawCommand, &DrawGeometry, &context) >= 0;
}
