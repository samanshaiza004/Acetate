/*  SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef AUDACITY_USE_ALICORN_SNAP_UI

#include "AlicornScrollBar.h"

#include <algorithm>
#include <cmath>

#include <wx/dcclient.h>
#include <wx/event.h>

#include "../tracks/ui/RetainedTrackControls.h"

namespace {
constexpr int kThickness = 15;
constexpr int kMinThumb = 24;
constexpr int kInset = 3;
}

AlicornScrollBar::AlicornScrollBar(
   wxWindow *parent, wxWindowID id, long style)
   : wxControl(
        parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
   , mVertical{ (style & wxSB_VERTICAL) != 0 }
{
   SetBackgroundStyle(wxBG_STYLE_PAINT);
   SetMinSize(mVertical ? wxSize(kThickness, -1) : wxSize(-1, kThickness));
   Bind(wxEVT_PAINT, &AlicornScrollBar::OnPaint, this);
   Bind(wxEVT_LEFT_DOWN, &AlicornScrollBar::OnLeftDown, this);
   Bind(wxEVT_LEFT_UP, &AlicornScrollBar::OnLeftUp, this);
   Bind(wxEVT_MOTION, &AlicornScrollBar::OnMotion, this);
   Bind(wxEVT_LEAVE_WINDOW, &AlicornScrollBar::OnLeave, this);
   Bind(wxEVT_MOUSE_CAPTURE_LOST, &AlicornScrollBar::OnCaptureLost, this);
}

wxSize AlicornScrollBar::DoGetBestSize() const
{
   return mVertical ? wxSize(kThickness, 50) : wxSize(50, kThickness);
}

void AlicornScrollBar::SetThumbPosition(int position)
{
   const auto metrics = ComputeMetrics();
   const auto clamped = std::clamp(position, 0, std::max(0, metrics.maxPosition));
   if (clamped == mPosition)
      return;
   mPosition = clamped;
   Refresh(false);
}

void AlicornScrollBar::SetScrollbar(
   int position, int thumbSize, int range, int pageSize, bool refresh)
{
   const bool changed = position != mPosition || thumbSize != mThumbSize ||
      range != mRange || pageSize != mPageSize;
   if (!changed)
      return;
   mThumbSize = std::max(0, thumbSize);
   mRange = std::max(0, range);
   mPageSize = std::max(0, pageSize);
   mPosition = std::clamp(position, 0, std::max(0, mRange - mThumbSize));
   if (refresh)
      Refresh(false);
}

AlicornScrollBar::Metrics AlicornScrollBar::ComputeMetrics() const
{
   Metrics m;
   const auto size = GetClientSize();
   m.length = mVertical ? size.y : size.x;
   m.maxPosition = std::max(0, mRange - mThumbSize);
   m.scrollable = mRange > 0 && mThumbSize < mRange && m.length > 0;
   if (!m.scrollable) {
      m.thumbLength = m.length;
      return m;
   }
   m.thumbLength = std::clamp(
      static_cast<int>(std::lround(
         static_cast<double>(m.length) * mThumbSize / mRange)),
      std::min(kMinThumb, m.length), m.length);
   const int travel = m.length - m.thumbLength;
   m.thumbStart = m.maxPosition > 0
      ? static_cast<int>(std::lround(
           static_cast<double>(travel) * mPosition / m.maxPosition))
      : 0;
   return m;
}

int AlicornScrollBar::Coordinate(const wxPoint &point) const
{
   return mVertical ? point.y : point.x;
}

void AlicornScrollBar::Send(wxEventType type)
{
   wxScrollEvent event(
      type, GetId(), mPosition, mVertical ? wxVERTICAL : wxHORIZONTAL);
   event.SetEventObject(this);
   GetEventHandler()->ProcessEvent(event);
}

void AlicornScrollBar::SetPositionClamped(int position)
{
   const auto metrics = ComputeMetrics();
   mPosition = std::clamp(position, 0, std::max(0, metrics.maxPosition));
}

void AlicornScrollBar::OnPaint(wxPaintEvent &)
{
   wxPaintDC dc(this);
   const auto size = GetClientSize();
   if (size.x <= 0 || size.y <= 0)
      return;
   const auto &RC = RetainedTrackControls::Mix;
   wxColour background = GetParent() ? GetParent()->GetBackgroundColour()
                                     : GetBackgroundColour();
   if (!background.IsOk())
      background = GetBackgroundColour();
   const auto trackColour = RC(background, *wxBLACK, 0.05);

   AlicornScene scene;
   scene.Fill(wxRect(0, 0, size.x, size.y), trackColour);
   const auto metrics = ComputeMetrics();
   if (metrics.scrollable) {
      const auto thumbColour =
         RC(background, *wxBLACK, (mDragging || mHovered) ? 0.62 : 0.45);
      const int across = (mVertical ? size.x : size.y) - 2 * kInset;
      if (across > 0) {
         const float radius = across * 0.5f;
         const int capLength = std::max(0, metrics.thumbLength - across);
         if (mVertical) {
            scene.Fill(
               wxRect(kInset, metrics.thumbStart + across / 2, across, capLength),
               thumbColour);
            scene.Dot(kInset + radius, metrics.thumbStart + radius, across, thumbColour);
            scene.Dot(
               kInset + radius,
               metrics.thumbStart + metrics.thumbLength - radius,
               across, thumbColour);
         }
         else {
            scene.Fill(
               wxRect(metrics.thumbStart + across / 2, kInset, capLength, across),
               thumbColour);
            scene.Dot(metrics.thumbStart + radius, kInset + radius, across, thumbColour);
            scene.Dot(
               metrics.thumbStart + metrics.thumbLength - radius,
               kInset + radius, across, thumbColour);
         }
      }
   }

   if (!mScene.Draw(dc, wxRect(0, 0, size.x, size.y), scene)) {
      dc.SetPen(*wxTRANSPARENT_PEN);
      dc.SetBrush(wxBrush(trackColour));
      dc.DrawRectangle(0, 0, size.x, size.y);
      if (metrics.scrollable) {
         dc.SetBrush(wxBrush(RC(background, *wxBLACK, 0.45)));
         if (mVertical)
            dc.DrawRoundedRectangle(
               kInset, metrics.thumbStart, size.x - 2 * kInset,
               metrics.thumbLength, 4);
         else
            dc.DrawRoundedRectangle(
               metrics.thumbStart, kInset, metrics.thumbLength,
               size.y - 2 * kInset, 4);
      }
   }
}

void AlicornScrollBar::OnLeftDown(wxMouseEvent &event)
{
   const auto metrics = ComputeMetrics();
   if (!metrics.scrollable)
      return;
   const int c = Coordinate(event.GetPosition());
   if (c >= metrics.thumbStart && c < metrics.thumbStart + metrics.thumbLength) {
      mDragging = true;
      mGrabOffset = c - metrics.thumbStart;
      CaptureMouse();
      Refresh(false);
      return;
   }
   const bool before = c < metrics.thumbStart;
   const int page = std::max(1, mPageSize > 0 ? mPageSize : mThumbSize);
   SetPositionClamped(mPosition + (before ? -page : page));
   Refresh(false);
   Send(before ? wxEVT_SCROLL_PAGEUP : wxEVT_SCROLL_PAGEDOWN);
   Send(wxEVT_SCROLL_CHANGED);
}

void AlicornScrollBar::OnMotion(wxMouseEvent &event)
{
   if (!mDragging) {
      if (!mHovered) {
         mHovered = true;
         Refresh(false);
      }
      return;
   }
   const auto metrics = ComputeMetrics();
   const int travel = metrics.length - metrics.thumbLength;
   if (travel <= 0 || metrics.maxPosition <= 0)
      return;
   const int start = std::clamp(
      Coordinate(event.GetPosition()) - mGrabOffset, 0, travel);
   const int position = static_cast<int>(std::lround(
      static_cast<double>(start) * metrics.maxPosition / travel));
   if (position == mPosition)
      return;
   mPosition = position;
   Refresh(false);
   Send(wxEVT_SCROLL_THUMBTRACK);
}

void AlicornScrollBar::OnLeftUp(wxMouseEvent &)
{
   if (!mDragging)
      return;
   mDragging = false;
   if (HasCapture())
      ReleaseMouse();
   Refresh(false);
   Send(wxEVT_SCROLL_THUMBRELEASE);
   Send(wxEVT_SCROLL_CHANGED);
}

void AlicornScrollBar::OnLeave(wxMouseEvent &)
{
   if (mHovered && !mDragging) {
      mHovered = false;
      Refresh(false);
   }
}

void AlicornScrollBar::OnCaptureLost(wxMouseCaptureLostEvent &)
{
   mDragging = false;
   Refresh(false);
}

#endif
