/**********************************************************************

Audacity: A Digital Audio Editor

EditCursorOverlay.cpp

Paul Licameli split from TrackPanel.cpp

**********************************************************************/


#include "EditCursorOverlay.h"

#include "ChannelView.h"
#include "AColor.h"
#include "../../AdornedRulerPanel.h"
#include "Project.h"
#include "../../ProjectWindow.h"
#include "../../ProjectWindows.h"
#include "Track.h" //
#include "TrackFocus.h"
#include "../../TrackPanel.h"
#include "ViewInfo.h"

#ifdef AUDACITY_USE_ALICORN_SNAP_UI
#include "../../alicorn/AlicornTimelineView.h"
#endif

#include <wx/dc.h>

namespace {
   template < class LOW, class MID, class HIGH >
   bool between_incexc(LOW l, MID m, HIGH h)
   {
      return (m >= l && m < h);
   }
}

static const AudacityProject::AttachedObjects::RegisteredFactory sOverlayKey{
  []( AudacityProject &parent ){
     auto result = std::make_shared< EditCursorOverlay >( &parent );
     TrackPanel::Get( parent ).AddOverlay( result );
     return result;
   }
};

EditCursorOverlay::EditCursorOverlay(AudacityProject *project, bool isMaster)
   : mProject(project)
   , mIsMaster(isMaster)
   , mLastCursorX(-1)
   , mCursorTime(-1)
   , mNewCursorX(-1)
{
}

unsigned EditCursorOverlay::SequenceNumber() const
{
   return 20;
}

std::pair<wxRect, bool> EditCursorOverlay::DoGetRectangle(wxSize size)
{
   const auto &viewInfo = ViewInfo::Get( *mProject );
   const auto &selection = viewInfo.selectedRegion;
   if (!selection.isPoint()) {
      mCursorTime = -1.0;
      mNewCursorX = -1;
   }
   else {
      mCursorTime = selection.t0();
      mNewCursorX = viewInfo.TimeToPosition(
         mCursorTime, viewInfo.GetLeftOffset());
   }

   // Excessive height in case of the ruler, but it matters little.
   return std::make_pair(
      mLastCursorX == -1
         ? wxRect()
         : wxRect(mLastCursorX, 0, 1, size.GetHeight()),
      mLastCursorX != mNewCursorX
   );
}


void EditCursorOverlay::Draw(OverlayPanel &panel, wxDC &dc)
{
#ifdef AUDACITY_USE_ALICORN_SNAP_UI
   const auto setRulerIndicator = [&](int x) {
      if (mIsMaster)
         return;
      if (auto ruler = dynamic_cast<AdornedRulerPanel*>(&panel)) {
         if (auto* window = ProjectWindow::Find(mProject)) {
            if (auto* view = window->FindAlicornTimelineView())
               (void)view->DrawRulerIndicator(
                  dc, ruler->GetInnerRect(), x, wxColour{0, 0, 0});
         }
      }
   };
#endif

   if (mIsMaster && !mPartner) {
      auto &ruler = AdornedRulerPanel::Get( *mProject );
      mPartner = std::make_shared<EditCursorOverlay>(mProject, false);
      ruler.AddOverlay( mPartner );
   }

   mLastCursorX = mNewCursorX;
   if (mLastCursorX == -1) {
#ifdef AUDACITY_USE_ALICORN_SNAP_UI
      setRulerIndicator(-1);
#endif
      return;
   }

   const auto &viewInfo = ViewInfo::Get( *mProject );

   const bool
   onScreen = between_incexc(viewInfo.hpos,
                             mCursorTime,
                             viewInfo.GetScreenEndTime());

   if (!onScreen) {
#ifdef AUDACITY_USE_ALICORN_SNAP_UI
      setRulerIndicator(-1);
#endif
      return;
   }

   auto &trackPanel = TrackPanel::Get( *mProject );
   //NOTE: point selection cursor drawing over tracks moved to TrackPanel.cpp(see also TrackArt::DrawCursor)
   /*if (auto tp = dynamic_cast<TrackPanel*>(&panel)) {
      wxASSERT(mIsMaster);
      AColor::CursorColor(&dc);

      // Draw cursor in all selected tracks
      tp->VisitCells( [&]( const wxRect &rect, TrackPanelCell &cell ) {
         const auto pChannelView = dynamic_cast<ChannelView*>(&cell);
         if (!pChannelView)
            return;
         const auto pChannel = pChannelView->FindChannel();
         const auto pTrack =
            dynamic_cast<Track *>(&pChannel->GetChannelGroup());
         if (pChannel && (pTrack->GetSelected() ||
             TrackFocus::Get( *mProject ).IsFocused(pTrack)))
         {
            // AColor::Line includes both endpoints so use GetBottom()
            AColor::Line(dc, mLastCursorX, rect.GetTop(), mLastCursorX, rect.GetBottom());
            // ^^^ The whole point of this routine.

         }
      } );
   }
   else if (auto ruler = dynamic_cast<AdornedRulerPanel*>(&panel)) {
      wxASSERT(!mIsMaster);
      dc.SetPen(*wxBLACK_PEN);
      // AColor::Line includes both endpoints so use GetBottom()
      auto rect = ruler->GetInnerRect();
      AColor::Line(dc, mLastCursorX, rect.GetTop(), mLastCursorX, rect.GetBottom());
   }
   else
      wxASSERT(false);*/
   if (auto ruler = dynamic_cast<AdornedRulerPanel*>(&panel)) {
       wxASSERT(!mIsMaster);
       // AColor::Line includes both endpoints so use GetBottom()
       auto rect = ruler->GetInnerRect();
#ifdef AUDACITY_USE_ALICORN_SNAP_UI
       if (auto* window = ProjectWindow::Find(mProject))
          if (auto* view = window->FindAlicornTimelineView();
              view && view->DrawRulerIndicator(dc, rect, mLastCursorX, wxColour{0, 0, 0}))
             return;
#endif
       dc.SetPen(*wxBLACK_PEN);
       AColor::Line(dc, mLastCursorX, rect.GetTop(), mLastCursorX, rect.GetBottom());
   }
}
