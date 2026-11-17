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
