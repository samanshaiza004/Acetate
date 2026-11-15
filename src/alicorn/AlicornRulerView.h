/*  SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <memory>

#include <wx/dc.h>
#include <wx/gdicmn.h>

#include "AudacityRulerView.h"
#include "widgets/Ruler.h"

class AlicornRulerView final {
public:
   explicit AlicornRulerView(const wxSize& size);
   ~AlicornRulerView();

   AlicornRulerView(const AlicornRulerView&) = delete;
   AlicornRulerView& operator=(const AlicornRulerView&) = delete;

   bool IsReady() const noexcept { return mView != nullptr; }
   bool Resize(const wxSize& size);
   bool Draw(
      wxDC& dc,
      const wxRect& clip,
      const Ruler::DisplaySnapshot& snapshot);

private:
   bool Update(const Ruler::DisplaySnapshot& snapshot);

   AudacityAlicornRulerView* mView{};
   std::unique_ptr<Ruler::DisplaySnapshot> mLastSnapshot;
};
