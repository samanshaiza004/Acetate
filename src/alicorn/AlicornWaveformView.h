/*  SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <cstddef>

#include <wx/dc.h>
#include <wx/gdicmn.h>

#include "AudacityWaveformView.h"

class AlicornWaveformView final {
public:
   explicit AlicornWaveformView(const wxSize& size);
   ~AlicornWaveformView();

   AlicornWaveformView(const AlicornWaveformView&) = delete;
   AlicornWaveformView& operator=(const AlicornWaveformView&) = delete;

   bool IsReady() const noexcept { return mView != nullptr; }
   bool Resize(const wxSize& size);
   bool Draw(
      wxDC& dc,
      const wxRect& clip,
      const AudacityWaveformSegment* segments,
      size_t count);

private:
   AudacityAlicornWaveformView* mView{};
};
