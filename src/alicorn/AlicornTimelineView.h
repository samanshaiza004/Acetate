/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <cstddef>
#include <array>
#include <memory>
#include <vector>

#include <wx/dc.h>
#include <wx/gdicmn.h>
#include <wx/colour.h>

#include "AudacityTimelineView.h"
#include "widgets/Ruler.h"

class AlicornTimelineView final {
public:
   struct RulerFill {
      wxRect bounds;
      wxColour color;
   };

   AlicornTimelineView() = default;
   explicit AlicornTimelineView(const wxSize& rulerSize, const wxSize& waveformSize);
   ~AlicornTimelineView();

   AlicornTimelineView(const AlicornTimelineView&) = delete;
   AlicornTimelineView& operator=(const AlicornTimelineView&) = delete;

   bool IsReady() const noexcept { return mView != nullptr; }
   bool Resize(const wxSize& rulerSize, const wxSize& waveformSize);
   bool ResizeRuler(const wxSize& size);
   bool ResizeWaveform(const wxSize& size);

   int32_t Pointer(
      uint32_t kind,
      float x,
      float y,
      uint32_t button,
      AudacityWaveformSelection& selection);
   int32_t RulerPointer(
      uint32_t kind,
      float x,
      float y,
      uint32_t button,
      AudacityWaveformSelection& selection);
   bool DrawWaveform(
      wxDC& dc,
      const wxRect& clip,
      const AudacityWaveformRectangle* rectangles,
      size_t rectangleCount,
      const AudacityWaveformSegment* segments,
      size_t count);
   bool DrawVerticalIndicator(
      wxDC& dc,
      const wxRect& clip,
      int x,
      const wxColour& color);
   bool DrawRuler(
      wxDC& dc,
      const wxRect& clip,
      const Ruler::DisplaySnapshot& snapshot,
      const RulerFill* fills = nullptr,
      size_t fillCount = 0);

private:
   bool UpdateRuler(const Ruler::DisplaySnapshot& snapshot, bool force = false);

   AudacityAlicornTimelineView* mView{};
   wxSize mRulerSize;
   wxSize mWaveformSize;
   std::unique_ptr<Ruler::DisplaySnapshot> mLastSnapshot;
   std::vector<AudacityRulerTick> mRulerTicks;
   std::array<RulerFill, AUDACITY_RULER_MAX_FILLS> mLastRulerFills{};
   size_t mLastRulerFillCount{};
   bool mHasRulerFills{};
   bool mRulerNeedsUpdate{ true };
};
