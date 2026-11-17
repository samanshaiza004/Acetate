/*  SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#ifdef AUDACITY_USE_ALICORN_SNAP_UI

#include <functional>

#include <wx/control.h>
#include <wx/frame.h>

#include "../alicorn/AlicornTimelineView.h"

class wxMenu;
class wxMenuBar;

// An in-window menu strip painted through a retained Alicorn scene. It reads
// the frame's existing wxMenuBar (labels, items, check/enable state, shortcut
// text), so the menus stay defined in one place, and activates items by
// sending the same wxEVT_MENU the native menu would. The native menu bar stays
// attached: it remains the source of truth and keeps platform accelerators,
// and removing it belongs with replacing the host window.
class AlicornMenuBar final : public wxControl {
public:
   AlicornMenuBar(wxWindow *parent, wxFrame *frame);

   // Called just before a menu opens so enabled/checked state is current.
   void SetBeforeOpen(std::function<void()> callback)
   {
      mBeforeOpen = std::move(callback);
   }

   bool AcceptsFocus() const override { return false; }

   // Opens the first menu, for keyboard access (F10).
   void OpenFirst() { OpenMenu(0); }

protected:
   wxSize DoGetBestSize() const override;

private:
   struct Title {
      wxString label;
      wxRect bounds;
   };
   std::vector<Title> Titles() const;
   int TitleAt(const wxPoint &point) const;
   void OpenMenu(int index);
   void OnMenuClosed();

   void OnPaint(wxPaintEvent &event);
   void OnLeftDown(wxMouseEvent &event);
   void OnMotion(wxMouseEvent &event);
   void OnLeave(wxMouseEvent &event);

   AlicornSceneView mScene;
   wxFrame *mFrame;
   std::function<void()> mBeforeOpen;
   int mOpenIndex{ -1 };
   int mHoverIndex{ -1 };
};

#endif
