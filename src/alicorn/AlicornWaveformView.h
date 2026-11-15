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
   int32_t Pointer(
      uint32_t kind,
      float x,
      float y,
      uint32_t button,
      AudacityWaveformSelection& selection);
   bool Draw(
      wxDC& dc,
      const wxRect& clip,
      const AudacityWaveformSegment* segments,
      size_t count);

private:
   AudacityAlicornWaveformView* mView{};
};
