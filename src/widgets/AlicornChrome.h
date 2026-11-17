/*  SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

// Helpers for painting simple window chrome (toolbar backgrounds, grabbers,
// dock lines) through a retained Alicorn scene, with one scene view per wx
// window. The caller keeps its wx drawing as the fallback.

#ifdef AUDACITY_USE_ALICORN_SNAP_UI

#include <functional>
#include <memory>
#include <unordered_map>

#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#include <wx/event.h>
#include <wx/window.h>

#include "AColor.h"
#include "AllThemeResources.h"
#include "Theme.h"
#include "../alicorn/AlicornTimelineView.h"

namespace AlicornChrome {

inline AlicornSceneView& ViewFor(wxWindow& window)
{
   static std::unordered_map<const wxWindow*, std::unique_ptr<AlicornSceneView>> views;
   const auto key = &window;
   auto it = views.find(key);
   if (it == views.end()) {
      it = views.emplace(key, std::make_unique<AlicornSceneView>()).first;
      window.Bind(wxEVT_DESTROY, [key](wxWindowDestroyEvent& event) {
         if (event.GetEventObject() == key)
            views.erase(key);
         event.Skip();
      });
   }
   return *it->second;
}

// Builds a scene covering the window's client area and paints it. Returns
// false when the retained path failed so the caller can draw with wx.
inline bool Paint(
   wxWindow& window, wxDC& dc, const std::function<void(AlicornScene&)>& build)
{
   const auto size = dc.GetSize();
   if (size.x <= 0 || size.y <= 0)
      return false;
   AlicornScene scene;
   build(scene);
   return ViewFor(window).Draw(dc, wxRect(0, 0, size.x, size.y), scene);
}

inline wxColour PenColourOf(const std::function<void(wxDC*)>& select)
{
   wxBitmap bitmap(1, 1);
   wxMemoryDC dc(bitmap);
   select(&dc);
   return dc.GetPen().GetColour();
}

inline wxColour DarkColour()
{
   return PenColourOf([](wxDC* dc) { AColor::Dark(dc, false); });
}

inline wxColour LightColour()
{
   return PenColourOf([](wxDC* dc) { AColor::Light(dc, false); });
}

inline wxColour MediumColour(bool over)
{
   return theTheme.Colour(over ? clrMediumSelected : clrMedium);
}

} // namespace AlicornChrome

#endif
