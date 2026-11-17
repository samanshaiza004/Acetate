/*  SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

// Presents an LWSlider as retained geometry for the opt-in Alicorn build. The
// slider keeps its value, geometry and mouse handling; this only mirrors
// LWSlider::OnPaint into a scene.

#ifdef AUDACITY_USE_ALICORN_SNAP_UI

#include <algorithm>

#include <wx/font.h>

#include "ASlider.h"
#include "../tracks/ui/RetainedTrackControls.h"

// Presents an LWSlider as retained geometry. The slider keeps its value,
// geometry and mouse handling; this only mirrors LWSlider::OnPaint.
struct RetainedLWSlider {
   static void Draw(
      LWSlider &slider, RetainedTrackControls::Scene &scene, bool highlight)
   {
      const auto panel = RetainedTrackControls::PanelColour(false);
      const auto text = theTheme.Colour(clrTrackPanelText);
      const auto trackColour = RetainedTrackControls::Mix(
         theTheme.Colour(clrSliderMain), *wxWHITE, 0.25);
      const int left = slider.mLeft;
      const int top = slider.mTop;
      if (slider.mDrawTrack && slider.mOrientation == wxHORIZONTAL) {
         scene.Line(
            { left + slider.mLeftX, top + slider.mCenterY },
            { left + slider.mRightX + 2, top + slider.mCenterY },
            trackColour);
         scene.Line(
            { left + slider.mLeftX, top + slider.mCenterY + 1 },
            { left + slider.mRightX + 2, top + slider.mCenterY + 1 },
            trackColour);
      }
      if (slider.mShowLabels && slider.mOrientation == wxHORIZONTAL) {
         if (slider.mStyle == PAN_SLIDER) {
            const wxFont labelFont(
               7, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
            const auto height = labelFont.GetPixelSize().GetHeight();
            scene.Text(
               wxRect(left + slider.mLeftX, top, 10, height),
               _("L"), text, labelFont);
            scene.Text(
               wxRect(left + slider.mRightX - 7, top, 10, height),
               _("R"), text, labelFont);
         }
         else {
            const auto y = top + slider.mCenterY - 10;
            scene.Line({ left + slider.mLeftX, y }, { left + slider.mLeftX + 4, y }, text);
            scene.Line({ left + slider.mRightX - 5, y }, { left + slider.mRightX - 1, y }, text);
            scene.Line(
               { left + slider.mRightX - 3, y - 2 },
               { left + slider.mRightX - 3, y + 2 }, text);
         }
      }
      // Tick marks under the track (horizontal sliders), as LWSlider draws.
      if (slider.mDrawTicks && slider.mOrientation == wxHORIZONTAL &&
          slider.mWidthX > 1) {
         const auto light = theTheme.Colour(clrSliderLight);
         if (light != wxColour(60, 60, 60)) {
            const auto dark = theTheme.Colour(clrSliderDark);
            const int divs = (slider.mWidth - 1) / 10;
            const double upp = divs / static_cast<double>(slider.mWidthX - 1);
            double d = 0.0;
            int intD = -1;
            for (int p = 0; p <= slider.mWidthX; ++p) {
               if (static_cast<int>(d) > intD) {
                  intD = static_cast<int>(d);
                  const int length = (intD == 0 || intD == divs) ? 5 : 3;
                  const int x = left + slider.mLeftX + p;
                  const int y = top + slider.mCenterY;
                  scene.Fill(wxRect(x, y - length, 1, length), light);
                  scene.Fill(wxRect(x + 1, y - length + 1, 1, length - 1), dark);
               }
               d += upp;
            }
         }
      }
      const int thumbPos =
         slider.ValueToPosition(slider.mCurrentValue) + 1 - slider.mThumbWidth / 2;
      const float centerX = left + thumbPos + slider.mThumbWidth * 0.5f;
      const float centerY = top + slider.mCenterY + 0.5f;
      const auto diameter = static_cast<float>(
         std::max(8, std::min(slider.mThumbWidth, slider.mThumbHeight)));
      scene.Dot(
         centerX, centerY, diameter + 2.0f,
         RetainedTrackControls::Mix(panel, *wxBLACK, 0.6));
      scene.Dot(
         centerX, centerY, diameter,
         highlight ? wxColour(235, 235, 235)
                   : RetainedTrackControls::Mix(text, panel, 0.15));
   }
};
#endif
