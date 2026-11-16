/**********************************************************************

Audacity: A Digital Audio Editor

ZoomHandle.cpp

Paul Licameli split from TrackPanel.cpp

**********************************************************************/


#include "ZoomHandle.h"

#include <algorithm>

#include <wx/dc.h>
#include <wx/event.h>
#include <wx/gdicmn.h>

#include "../../HitTestResult.h"
#include "../../RefreshCode.h"
#include "../../TrackArtist.h"
#include "../../TrackPanelDrawingContext.h"
#include "../../TrackPanelMouseEvent.h"
#include "ViewInfo.h"
#ifdef AUDACITY_USE_ALICORN_SNAP_UI
#include "../../TrackPanel.h"
#include "../../alicorn/AlicornTimelineView.h"
#endif
#include "../../../images/Cursors.h"

#ifdef AUDACITY_USE_ALICORN_SNAP_UI
namespace
{
std::uint32_t AlicornButton(const wxMouseEvent& event)
{
   if (event.LeftDown() || event.LeftDClick())
      return AUDACITY_SNAP_BUTTON_PRIMARY;
   if (event.MiddleDown())
      return AUDACITY_SNAP_BUTTON_MIDDLE;
   if (event.RightDown())
      return AUDACITY_SNAP_BUTTON_SECONDARY;
   return 0;
}
}
#endif

///  This class takes care of our different zoom
///  possibilities.  It is possible for a user to just
///  "zoom in" or "zoom out," but it is also possible
///  for a user to drag and select an area that he
///  or she wants to be zoomed in on.  We use mZoomStart
///  and mZoomEnd to track the beginning and end of such
///  a zoom area.  Note that the ViewInfo
///  actually keeps track of our zoom constant,
///  so we achieve zooming by altering the zoom constant
///  and forcing a refresh.

ZoomHandle::ZoomHandle()
{}

HitTestPreview ZoomHandle::HitPreview
   (const wxMouseState &state, const AudacityProject *WXUNUSED(pProject))
{
   static auto zoomInCursor =
      ::MakeCursor(wxCURSOR_MAGNIFIER, ZoomInCursorXpm, 19, 15);
   static auto zoomOutCursor =
      ::MakeCursor(wxCURSOR_MAGNIFIER, ZoomOutCursorXpm, 19, 15);
   TranslatableString message;
   // TODO:  Why not mention middle click to zoom normal on Windows too?
#if defined( __WXMAC__ )
   message = XO("Click to Zoom In, Shift-Click to Zoom Out");
#elif defined( __WXMSW__ )
   message = XO("Drag to Zoom Into Region, Right-Click to Zoom Out");
#elif defined( __WXGTK__ )
   message = XO("Left=Zoom In, Right=Zoom Out, Middle=Normal");
#endif
   return {
      message,
      (state.ShiftDown() ? &*zoomOutCursor : &*zoomInCursor)
   };
}

UIHandlePtr ZoomHandle::HitAnywhere
(std::weak_ptr<ZoomHandle> &holder)
{
   auto result = std::make_shared<ZoomHandle>();
   result = AssignUIHandlePtr(holder, result);
   return result;
}

UIHandlePtr ZoomHandle::HitTest
(std::weak_ptr<ZoomHandle> &holder,
 const wxMouseState &state)
{
   if (state.ButtonIsDown(wxMOUSE_BTN_RIGHT))
      return HitAnywhere(holder);
   else
      return {};
}

ZoomHandle::~ZoomHandle()
{
}

std::shared_ptr<const Track> ZoomHandle::FindTrack() const
{
   return nullptr;
}

bool ZoomHandle::HandlesRightClick()
{
   return true;
}

UIHandle::Result ZoomHandle::Click
(const TrackPanelMouseEvent &evt, AudacityProject *pProject)
{
   const wxMouseEvent &event = evt.event;
   if (event.ButtonDown() || event.LeftDClick()) {
      /// Zoom button down, record the position.
      mZoomStart = event.m_x;
      mZoomEnd = event.m_x;
      mRect = evt.rect;
#ifdef AUDACITY_USE_ALICORN_SNAP_UI
      mAlicornZoomActive = false;
      mAlicornZoomPreviewReady = false;
      mAlicornZoomButton = AlicornButton(event);
      if (mAlicornZoomButton != 0) {
         AudacityWaveformSelection gesture{};
         if (auto* view = TrackPanel::Get(*pProject).GetAlicornTimelineView()) {
            if (view->ZoomPointer(
                   AUDACITY_SNAP_POINTER_DOWN,
                   static_cast<float>(event.m_x),
                   static_cast<float>(event.m_y),
                   mAlicornZoomButton,
                   gesture) == AUDACITY_WAVEFORM_SELECTION_BEGAN) {
               mAlicornZoomActive = true;
               mZoomStart = static_cast<int>(gesture.anchor_x);
               mZoomEnd = static_cast<int>(gesture.current_x);
               mAlicornZoomPreviewReady = view->UpdateZoomPreview(
                  true, mZoomStart, mZoomEnd);
            }
            else
               view->UpdateZoomPreview(false, 0, 0);
         }
      }
#endif
   }
   return RefreshCode::RefreshNone;
}

UIHandle::Result ZoomHandle::Drag
(const TrackPanelMouseEvent &evt, AudacityProject *pProject)
{
   const wxMouseEvent &event = evt.event;
   const int left = mRect.GetLeft();
   const int right = mRect.GetRight();

   mZoomEnd = event.m_x;
#ifdef AUDACITY_USE_ALICORN_SNAP_UI
   if (mAlicornZoomActive) {
      AudacityWaveformSelection gesture{};
      if (auto* view = TrackPanel::Get(*pProject).GetAlicornTimelineView()) {
         if (view->ZoomPointer(
                AUDACITY_SNAP_POINTER_MOVE,
                static_cast<float>(event.m_x),
                static_cast<float>(event.m_y),
                mAlicornZoomButton,
                gesture) == AUDACITY_WAVEFORM_SELECTION_UPDATED)
            mZoomEnd = static_cast<int>(gesture.current_x);
      }
   }
#endif

   if (event.m_x < left) {
      mZoomEnd = left;
   }
   else if (event.m_x > right) {
      mZoomEnd = right;
   }

#ifdef AUDACITY_USE_ALICORN_SNAP_UI
   if (mAlicornZoomActive) {
      if (auto* view = TrackPanel::Get(*pProject).GetAlicornTimelineView())
         mAlicornZoomPreviewReady = view->UpdateZoomPreview(
            true, mZoomStart, mZoomEnd);
      else
         mAlicornZoomPreviewReady = false;
   }
#endif

   // Refresh tracks ALWAYS.  Even if IsDragZooming() becomes false, make the
   // dashed lines disappear. -- PRL
   return RefreshCode::RefreshAll; // (IsDragZooming() ? RefreshAllTracks : RefreshNone),
}

HitTestPreview ZoomHandle::Preview
(const TrackPanelMouseState &st, AudacityProject *pProject)
{
   return HitPreview(st.state, pProject);
}

UIHandle::Result ZoomHandle::Release
(const TrackPanelMouseEvent &evt, AudacityProject *pProject,
 wxWindow *)
{
   const wxMouseEvent &event = evt.event;
   auto &viewInfo = ViewInfo::Get( *pProject );
#ifdef AUDACITY_USE_ALICORN_SNAP_UI
   if (mAlicornZoomActive) {
      AudacityWaveformSelection gesture{};
      if (auto* view = TrackPanel::Get(*pProject).GetAlicornTimelineView()) {
         if (view->ZoomPointer(
                AUDACITY_SNAP_POINTER_UP,
                static_cast<float>(event.m_x),
                static_cast<float>(event.m_y),
                mAlicornZoomButton,
                gesture) == AUDACITY_WAVEFORM_SELECTION_ENDED) {
            mZoomStart = static_cast<int>(gesture.anchor_x);
            mZoomEnd = std::clamp(
               static_cast<int>(gesture.current_x),
               mRect.GetLeft(), mRect.GetRight());
         }
      }
   }
   mAlicornZoomActive = false;
   mAlicornZoomButton = 0;
#endif
   if (mZoomEnd < mZoomStart)
      std::swap(mZoomStart, mZoomEnd);

   const int trackLeftEdge = mRect.x;
   if (IsDragZooming())
   {
      ///  This actually sets the Zoom value when you're done doing
      ///  a drag zoom.
      double left = viewInfo.PositionToTime(mZoomStart, trackLeftEdge);
      double right = viewInfo.PositionToTime(mZoomEnd, trackLeftEdge);

      double multiplier =
         (viewInfo.PositionToTime(mRect.width) - viewInfo.PositionToTime(0)) /
         (right - left);
      if (event.ShiftDown())
         multiplier = 1.0 / multiplier;

      viewInfo.ZoomBy(multiplier);

      viewInfo.hpos = left;
   }
   else
   {
      /// This handles normal Zoom In/Out, if you just clicked;
      /// IOW, if you were NOT dragging to zoom an area.
      /// \todo MAGIC NUMBER: We've got several in this method.
      const double center_h =
         viewInfo.PositionToTime(event.m_x, trackLeftEdge);

      const double multiplier =
         (event.RightUp() || event.RightDClick() || event.ShiftDown())
         ? 0.5 : 2.0;
      viewInfo.ZoomBy(multiplier);

      if (event.MiddleUp() || event.MiddleDClick())
         viewInfo.SetZoom(ZoomInfo::GetDefaultZoom()); // AS: Reset zoom.

      const double new_center_h =
         viewInfo.PositionToTime(event.m_x, trackLeftEdge);

      viewInfo.hpos += (center_h - new_center_h);
   }

   mZoomEnd = mZoomStart = 0;

#ifdef AUDACITY_USE_ALICORN_SNAP_UI
   mAlicornZoomPreviewReady = false;
   if (auto* view = TrackPanel::Get(*pProject).GetAlicornTimelineView())
      view->UpdateZoomPreview(false, 0, 0);
#endif

   using namespace RefreshCode;
   return RefreshAll | FixScrollbars;
}

UIHandle::Result ZoomHandle::Cancel(AudacityProject* pProject)
{
#ifdef AUDACITY_USE_ALICORN_SNAP_UI
   if (mAlicornZoomActive) {
      AudacityWaveformSelection gesture{};
      if (pProject) {
         if (auto* view = TrackPanel::Get(*pProject).GetAlicornTimelineView()) {
            view->ZoomPointer(
               AUDACITY_SNAP_POINTER_CANCEL, 0.0f, 0.0f, 0, gesture);
            view->UpdateZoomPreview(false, 0, 0);
         }
      }
      mAlicornZoomActive = false;
      mAlicornZoomPreviewReady = false;
      mAlicornZoomButton = 0;
   }
#endif
   mZoomStart = mZoomEnd = 0;
   // Cancel is implemented!  And there is no initial state to restore,
   // so just return a code.
   return RefreshCode::RefreshAll;
}

void ZoomHandle::Draw(
   TrackPanelDrawingContext &context,
   const wxRect &rect, unsigned iPass )
{
   if ( iPass == TrackArtist::PassZooming &&
       // PRL: Draw dashed lines only if we would zoom in
       // for button up.
       IsDragZooming() ) {
      auto &dc = context.dc;
#ifdef AUDACITY_USE_ALICORN_SNAP_UI
      if (mAlicornZoomActive && mAlicornZoomPreviewReady) {
         if (auto* artist = TrackArtist::Get(context)) {
            if (artist->parent) {
               if (auto* view = artist->parent->GetAlicornTimelineView()) {
                  if (view->DrawZoomPreview(dc, rect))
                     return;
               }
            }
         }
      }
#endif
      dc.SetBrush(*wxTRANSPARENT_BRUSH);
      dc.SetPen(*wxBLACK_DASHED_PEN);
      // Make the top and bottom of the dashed rectangle disappear out of
      // bounds, so that you only see vertical dashed lines.
      dc.DrawRectangle( {
         std::min(mZoomStart, mZoomEnd),
         rect.y - 1,
         1 + abs(mZoomEnd - mZoomStart),
         rect.height + 2
      } );
   }
}

wxRect ZoomHandle::DrawingArea(
   TrackPanelDrawingContext &,
   const wxRect &rect, const wxRect &panelRect, unsigned iPass )
{
   if ( iPass == TrackArtist::PassZooming )
      return MaximizeHeight( rect, panelRect );
   else
      return rect;
}

bool ZoomHandle::IsDragZooming() const
{
   const int DragThreshold = 3;// Anything over 3 pixels is a drag, else a click.
   return (abs(mZoomEnd - mZoomStart) > DragThreshold);
}
