/*  SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

// Helpers that let the track-control draw functions emit retained Alicorn
// geometry and text instead of drawing with wx, when the opt-in Alicorn build
// is active. Hit testing, state and policy stay with the track-panel cells.

#ifdef AUDACITY_USE_ALICORN_SNAP_UI

#include <algorithm>
#include <cmath>

#include <wx/colour.h>
#include <wx/gdicmn.h>

#include "AllThemeResources.h"
#include "Theme.h"
#include "TrackArtist.h"
#include "TrackPanel.h"
#include "AColor.h"
#include "TrackPanelDrawingContext.h"
#include "alicorn/AlicornTimelineView.h"

namespace RetainedTrackControls {

using Scene = AlicornTimelineView::TrackControlsScene;

inline Scene*& CurrentRef()
{
   static thread_local Scene* scene{};
   return scene;
}

// The scene the draw functions should emit into, or null to draw with wx.
inline Scene* Current() { return CurrentRef(); }

class Scope {
public:
   explicit Scope(Scene* scene) : mPrevious{ CurrentRef() }
   {
      CurrentRef() = scene;
   }
   ~Scope() { CurrentRef() = mPrevious; }
   Scope(const Scope&) = delete;
   Scope& operator=(const Scope&) = delete;

private:
   Scene* mPrevious;
};

inline AlicornTimelineView* ViewFor(TrackPanelDrawingContext& context)
{
   const auto artist = TrackArtist::Get(context);
   return artist && artist->parent ? artist->parent->GetAlicornTimelineView()
                                   : nullptr;
}

// Builds a scene and paints it through the project's retained runtime.
// Returns false when the retained path is unavailable so the caller can draw
// with wx. `clip` bounds the paint in track-panel coordinates.
template<typename Build>
inline bool Paint(
   TrackPanelDrawingContext& context, const wxRect& clip, Build&& build)
{
   auto* view = ViewFor(context);
   if (!view)
      return false;
   Scene scene;
   build(scene);
   if (scene.Empty())
      return true;
   return view->DrawTrackControls(context.dc, clip, scene);
}

// A one-pixel outline inside `r`, matching wxDC::DrawRectangle with a 1 px pen.
inline void Outline(Scene& scene, const wxRect& r, const wxColour& colour)
{
   if (r.width <= 0 || r.height <= 0)
      return;
   scene.Fill(wxRect(r.x, r.y, r.width, 1), colour);
   if (r.height > 1)
      scene.Fill(wxRect(r.x, r.y + r.height - 1, r.width, 1), colour);
   if (r.height > 2) {
      scene.Fill(wxRect(r.x, r.y + 1, 1, r.height - 2), colour);
      if (r.width > 1)
         scene.Fill(wxRect(r.x + r.width - 1, r.y + 1, 1, r.height - 2), colour);
   }
}

// A one-pixel rounded outline: straight edges between four quarter-circle
// corners approximated by short segments.
inline void RoundedOutline(
   Scene& scene, const wxRect& r, int radius, const wxColour& colour)
{
   if (r.width <= 0 || r.height <= 0)
      return;
   radius = std::max(0, std::min({ radius, r.width / 2, r.height / 2 }));
   if (radius == 0) {
      Outline(scene, r, colour);
      return;
   }
   const int left = r.x, top = r.y, right = r.GetRight(), bottom = r.GetBottom();
   scene.Fill(wxRect(left + radius, top, std::max(0, r.width - 2 * radius), 1), colour);
   scene.Fill(wxRect(left + radius, bottom, std::max(0, r.width - 2 * radius), 1), colour);
   scene.Fill(wxRect(left, top + radius, 1, std::max(0, r.height - 2 * radius)), colour);
   scene.Fill(wxRect(right, top + radius, 1, std::max(0, r.height - 2 * radius)), colour);
   constexpr double pi = 3.14159265358979323846;
   constexpr int steps = 6;
   const auto arc = [&](int cx, int cy, int sx, int sy) {
      wxPoint previous{ cx + sx * radius, cy };
      for (int i = 1; i <= steps; ++i) {
         const double angle = (pi / 2) * i / steps;
         const wxPoint point{
            cx + sx * static_cast<int>(std::lround(radius * std::cos(angle))),
            cy + sy * static_cast<int>(std::lround(radius * std::sin(angle))) };
         scene.Line(previous, point, colour);
         previous = point;
      }
   };
   arc(left + radius, top + radius, -1, -1);
   arc(right - radius, top + radius, 1, -1);
   arc(left + radius, bottom - radius, -1, 1);
   arc(right - radius, bottom - radius, 1, 1);
}

// A dashed outline, as wxBLACK_DASHED_PEN draws a rectangle.
inline void DashedOutline(Scene& scene, const wxRect& r, const wxColour& colour)
{
   constexpr int dash = 3;
   const auto horizontal = [&](int x0, int x1, int y) {
      for (int x = x0; x <= x1; x += 2 * dash)
         scene.Fill(wxRect(x, y, std::min(dash, x1 - x + 1), 1), colour);
   };
   const auto vertical = [&](int x, int y0, int y1) {
      for (int y = y0; y <= y1; y += 2 * dash)
         scene.Fill(wxRect(x, y, 1, std::min(dash, y1 - y + 1)), colour);
   };
   if (r.width <= 0 || r.height <= 0)
      return;
   horizontal(r.x, r.GetRight(), r.y);
   horizontal(r.x, r.GetRight(), r.GetBottom());
   vertical(r.x, r.y, r.GetBottom());
   vertical(r.GetRight(), r.y, r.GetBottom());
}

inline wxColour TrackPanelBackgroundColour(bool selected = false)
{
   return theTheme.Colour(selected ? clrMediumSelected : clrTrackBackground);
}

inline wxColour Mix(const wxColour& from, const wxColour& to, double amount)
{
   const auto channel = [&](unsigned char a, unsigned char b) {
      return static_cast<unsigned char>(
         std::lround(a + (static_cast<double>(b) - a) * amount));
   };
   return wxColour(
      channel(from.Red(), to.Red()),
      channel(from.Green(), to.Green()),
      channel(from.Blue(), to.Blue()));
}

inline wxColour PanelColour(bool selected)
{
   return theTheme.Colour(selected ? clrTrackInfoSelected : clrTrackInfo);
}

// A flat bevel-style button: 1 px border around a lighter or darker face.
inline void Button(
   Scene& scene, const wxRect& bounds, bool selected, bool hovered, bool down,
   const wxColour* activeFace = nullptr)
{
   const auto base = PanelColour(selected);
   const auto border = Mix(base, *wxBLACK, 0.55);
   wxColour face = activeFace ? *activeFace
      : down ? Mix(base, *wxBLACK, 0.25)
      : hovered ? Mix(base, *wxWHITE, 0.30)
      : Mix(base, *wxWHITE, 0.14);
   if (activeFace && hovered)
      face = Mix(face, *wxWHITE, 0.15);
   scene.Fill(bounds, border);
   scene.Fill(wxRect(bounds).Deflate(1), face);
}

// Hover/pressed backdrop used by the small title-bar tool buttons.
inline void ToolButtonBackground(
   Scene& scene, const wxRect& bounds, bool selected, bool captured)
{
   const auto base = PanelColour(selected);
   scene.Fill(bounds, Mix(base, *wxWHITE, captured ? 0.10 : 0.26));
}

inline void CloseGlyph(Scene& scene, const wxRect& bounds, const wxColour& color)
{
   const auto cx = bounds.x + bounds.width / 2;
   const auto cy = bounds.y + bounds.height / 2;
   scene.Line({ cx - 3, cy - 3 }, { cx + 3, cy + 3 }, color, 1.5f);
   scene.Line({ cx - 3, cy + 3 }, { cx + 3, cy - 3 }, color, 1.5f);
}

inline void ChevronGlyph(
   Scene& scene, const wxRect& bounds, bool pointingDown, const wxColour& color)
{
   const auto cx = bounds.x + bounds.width / 2;
   const auto cy = bounds.y + bounds.height / 2;
   const auto tip = pointingDown ? 2 : -2;
   scene.Line({ cx - 4, cy - tip }, { cx, cy + tip }, color, 1.5f);
   scene.Line({ cx, cy + tip }, { cx + 4, cy - tip }, color, 1.5f);
}

inline void EllipsisGlyph(Scene& scene, const wxRect& bounds, const wxColour& color)
{
   const auto cx = static_cast<float>(bounds.x + bounds.width / 2) + 0.5f;
   const auto cy = static_cast<float>(bounds.y + bounds.height / 2) + 0.5f;
   for (const auto dx : { -4.0f, 0.0f, 4.0f })
      scene.Dot(cx + dx, cy, 2.5f, color);
}

} // namespace RetainedTrackControls

#endif
