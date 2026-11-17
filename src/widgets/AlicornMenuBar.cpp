/*  SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef AUDACITY_USE_ALICORN_SNAP_UI

#include "AlicornMenuBar.h"

#include <algorithm>
#include <vector>

#include <wx/dcclient.h>
#include <wx/display.h>
#include <wx/menu.h>
#include <wx/popupwin.h>
#include <wx/settings.h>

#include <wx/weakref.h>

#include "AlicornMenu.h"
#include "../tracks/ui/RetainedTrackControls.h"

namespace {
struct Palette { wxColour text, line; };
constexpr int kBarHeight = 24;
constexpr int kTitlePad = 12;
constexpr int kRowHeight = 22;
constexpr int kSeparatorHeight = 7;
constexpr int kPadX = 10;

using RetainedTrackControls::Mix;

Palette MakePalette()
{
   Palette p;
   p.text = wxSystemSettings::GetColour(wxSYS_COLOUR_MENUTEXT);
   p.line = Mix(wxSystemSettings::GetColour(wxSYS_COLOUR_MENU), p.text, 0.25);
   return p;
}

// Sends the chosen item's id to the frame as the native menu would, finding
// the item again in the live menu bar so a rebuilt menu bar is safe.
void ActivateItem(const wxWeakRef<wxFrame> &frame, int itemId)
{
   if (!frame)
      return;
   auto *bar = frame->GetMenuBar();
   if (!bar)
      return;
   wxMenu *menu = nullptr;
   auto *item = bar->FindItem(itemId, &menu);
   if (!item || !menu)
      return;
   if (item->IsCheckable())
      item->Check(!item->IsChecked());
   wxCommandEvent event(wxEVT_MENU, itemId);
   event.SetEventObject(menu);
   if (item->IsCheckable())
      event.SetInt(item->IsChecked() ? 1 : 0);
   frame->GetEventHandler()->ProcessEvent(event);
}
}

AlicornMenuBar::AlicornMenuBar(wxWindow *parent, wxFrame *frame)
   : wxControl(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, kBarHeight),
               wxBORDER_NONE)
   , mFrame{ frame }
{
   SetBackgroundStyle(wxBG_STYLE_PAINT);
   SetMinSize(wxSize(-1, kBarHeight));
   SetName(wxT("Menu bar"));
   Bind(wxEVT_PAINT, &AlicornMenuBar::OnPaint, this);
   Bind(wxEVT_LEFT_DOWN, &AlicornMenuBar::OnLeftDown, this);
   Bind(wxEVT_MOTION, &AlicornMenuBar::OnMotion, this);
   Bind(wxEVT_LEAVE_WINDOW, &AlicornMenuBar::OnLeave, this);
}

wxSize AlicornMenuBar::DoGetBestSize() const
{
   return wxSize(100, kBarHeight);
}

std::vector<AlicornMenuBar::Title> AlicornMenuBar::Titles() const
{
   std::vector<Title> titles;
   auto *bar = mFrame ? mFrame->GetMenuBar() : nullptr;
   if (!bar)
      return titles;
   wxClientDC dc(const_cast<AlicornMenuBar *>(this));
   dc.SetFont(GetFont());
   int x = 4;
   for (size_t i = 0; i < bar->GetMenuCount(); ++i) {
      const auto label = bar->GetMenuLabelText(i);
      const int width = dc.GetTextExtent(label).x + 2 * kTitlePad;
      titles.push_back({ label, wxRect(x, 0, width, kBarHeight) });
      x += width;
   }
   return titles;
}

int AlicornMenuBar::TitleAt(const wxPoint &point) const
{
   const auto titles = Titles();
   for (size_t i = 0; i < titles.size(); ++i)
      if (titles[i].bounds.Contains(point))
         return static_cast<int>(i);
   return -1;
}

void AlicornMenuBar::OpenMenu(int index)
{
   auto *bar = mFrame ? mFrame->GetMenuBar() : nullptr;
   if (!bar || index < 0 || index >= static_cast<int>(bar->GetMenuCount()) ||
       mOpenIndex >= 0)
      return;
   if (mBeforeOpen)
      mBeforeOpen();
   auto *menu = bar->GetMenu(index);
   if (!menu)
      return;
   const auto titles = Titles();
   mOpenIndex = index;
   const int count = static_cast<int>(bar->GetMenuCount());
   wxWeakRef<wxFrame> frame{ mFrame };
   AlicornMenu::Options options;
   options.title = titles[index].label;
   options.journal = false;
   options.onClosed = [this](bool) { OnMenuClosed(); };
   options.onSwitch = [this, index, count](int direction) {
      OpenMenu((index + direction + count) % count);
   };
   AlicornMenu::Show(
      *this,
      ClientToScreen(wxPoint(titles[index].bounds.x, kBarHeight)),
      AlicornMenu::ModelFromMenu(
         *menu, [frame](int id) { ActivateItem(frame, id); }),
      std::move(options));
   Refresh(false);
}

void AlicornMenuBar::OnMenuClosed()
{
   mOpenIndex = -1;
   Refresh(false);
}

void AlicornMenuBar::OnPaint(wxPaintEvent &)
{
   wxPaintDC dc(this);
   const auto size = GetClientSize();
   if (size.x <= 0 || size.y <= 0)
      return;
   const auto palette = MakePalette();
   const auto background = wxColour(theTheme.Colour(clrMedium));
   dc.SetFont(GetFont());
   const auto titles = Titles();

   AlicornScene scene;
   scene.Fill(wxRect(0, 0, size.x, size.y), background);
   scene.Fill(wxRect(0, size.y - 1, size.x, 1), palette.line);
   const auto text = theTheme.Colour(clrTrackPanelText);
   for (size_t i = 0; i < titles.size(); ++i) {
      const bool active = static_cast<int>(i) == mOpenIndex ||
         static_cast<int>(i) == mHoverIndex;
      if (active)
         scene.Fill(titles[i].bounds, Mix(background, text, 0.18));
      const int textHeight = dc.GetTextExtent(titles[i].label).y;
      scene.Text(
         wxRect(titles[i].bounds.x + kTitlePad,
                (size.y - textHeight) / 2,
                titles[i].bounds.width - kTitlePad, textHeight),
         titles[i].label, text, GetFont());
   }
   if (!mScene.Draw(dc, wxRect(0, 0, size.x, size.y), scene)) {
      dc.SetBackground(wxBrush(background));
      dc.Clear();
      dc.SetTextForeground(text);
      for (const auto &title : titles)
         dc.DrawText(title.label, title.bounds.x + kTitlePad, 4);
   }
}

void AlicornMenuBar::OnLeftDown(wxMouseEvent &event)
{
   OpenMenu(TitleAt(event.GetPosition()));
}

void AlicornMenuBar::OnMotion(wxMouseEvent &event)
{
   const int index = TitleAt(event.GetPosition());
   if (index != mHoverIndex) {
      mHoverIndex = index;
      Refresh(false);
   }
}

void AlicornMenuBar::OnLeave(wxMouseEvent &)
{
   if (mHoverIndex != -1) {
      mHoverIndex = -1;
      Refresh(false);
   }
}

#endif
