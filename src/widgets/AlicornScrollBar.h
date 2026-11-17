/*  SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#ifdef AUDACITY_USE_ALICORN_SNAP_UI

#include <wx/control.h>

#include "ProjectScrollBar.h"
#include "../alicorn/AlicornTimelineView.h"

// A scrollbar painted through a retained Alicorn scene. It owns only the
// scrollbar's own position state and pointer gesture; Viewport applies the
// resulting scroll through the same wxScrollEvents a native scrollbar sends.
class AlicornScrollBar final : public wxControl, public ProjectScrollBar {
public:
   AlicornScrollBar(wxWindow *parent, wxWindowID id, long style);

   wxWindow *Window() override { return this; }
   int GetThumbPosition() const override { return mPosition; }
   int GetThumbSize() const override { return mThumbSize; }
   int GetRange() const override { return mRange; }
   int GetPageSize() const override { return mPageSize; }
   void SetThumbPosition(int position) override;
   void SetScrollbar(
      int position, int thumbSize, int range, int pageSize,
      bool refresh) override;

   bool AcceptsFocus() const override { return false; }
   bool AcceptsFocusFromKeyboard() const override { return false; }

protected:
   wxSize DoGetBestSize() const override;

private:
   struct Metrics {
      int length{};
      int thumbLength{};
      int thumbStart{};
      int maxPosition{};
      bool scrollable{};
   };

   Metrics ComputeMetrics() const;
   int Coordinate(const wxPoint &point) const;
   void Send(wxEventType type);
   void SetPositionClamped(int position);

   void OnPaint(wxPaintEvent &event);
   void OnLeftDown(wxMouseEvent &event);
   void OnLeftUp(wxMouseEvent &event);
   void OnMotion(wxMouseEvent &event);
   void OnLeave(wxMouseEvent &event);
   void OnCaptureLost(wxMouseCaptureLostEvent &event);

   AlicornSceneView mScene;
   const bool mVertical;
   int mPosition{};
   int mThumbSize{};
   int mRange{};
   int mPageSize{};
   bool mDragging{};
   bool mHovered{};
   int mGrabOffset{};
};

#endif
