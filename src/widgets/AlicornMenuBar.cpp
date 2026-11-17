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

#include "../tracks/ui/RetainedTrackControls.h"

namespace {
constexpr int kBarHeight = 24;
constexpr int kTitlePad = 12;
constexpr int kRowHeight = 22;
constexpr int kSeparatorHeight = 7;
constexpr int kPadX = 10;

using RetainedTrackControls::Mix;

struct Palette {
   wxColour background, text, disabled, highlight, highlightText, line;
};

Palette MakePalette()
{
   Palette p;
   p.background = wxSystemSettings::GetColour(wxSYS_COLOUR_MENU);
   p.text = wxSystemSettings::GetColour(wxSYS_COLOUR_MENUTEXT);
   p.disabled = Mix(p.text, p.background, 0.6);
   p.highlight = wxSystemSettings::GetColour(wxSYS_COLOUR_MENUHILIGHT);
   p.highlightText = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT);
   p.line = Mix(p.background, p.text, 0.25);
   return p;
}

struct Row {
   wxMenuItem *item{};   // null for the back header
   wxString label;
   wxString shortcut;
   bool separator{};
   bool enabled{ true };
   bool checkable{};
   bool checked{};
   bool submenu{};
   bool back{};
   int top{};
   int height{};
};

// Menu item labels may carry an accelerator after a tab.
void SplitLabel(wxMenuItem &item, wxString &label, wxString &shortcut)
{
   wxString full = item.GetItemLabel();
   const auto tab = full.Find('\t');
   if (tab != wxNOT_FOUND) {
      shortcut = full.Mid(tab + 1);
      full = full.Left(tab);
   }
   label = wxStripMenuCodes(full);
}
}

class AlicornMenuPopup final : public wxPopupTransientWindow {
public:
   AlicornMenuPopup(AlicornMenuBar *owner, wxMenu *root, const wxString &title)
      : wxPopupTransientWindow(owner, wxBORDER_SIMPLE)
      , mOwner{ owner }
      , mPalette{ MakePalette() }
   {
      SetBackgroundStyle(wxBG_STYLE_PAINT);
      mStack.push_back({ root, title });
      Build();
      Bind(wxEVT_PAINT, &AlicornMenuPopup::OnPaint, this);
      Bind(wxEVT_MOTION, &AlicornMenuPopup::OnMotion, this);
      Bind(wxEVT_LEFT_UP, &AlicornMenuPopup::OnLeftUp, this);
      Bind(wxEVT_MOUSEWHEEL, &AlicornMenuPopup::OnWheel, this);
      Bind(wxEVT_CHAR_HOOK, &AlicornMenuPopup::OnKey, this);
   }

   void ShowAt(const wxPoint &screen)
   {
      Position(screen, wxSize(0, 0));
      Popup();
   }

protected:
   void OnDismiss() override
   {
      wxPopupTransientWindow::OnDismiss();
      auto *owner = mOwner;
      wxMenu *activeMenu = mActiveMenu;
      const int activeId = mActiveId;
      owner->CallAfter([owner, activeMenu, activeId] {
         owner->PopupClosed();
         if (activeMenu)
            owner->Activate(activeMenu, activeId);
      });
   }

private:
   struct Level {
      wxMenu *menu;
      wxString title;
   };

   void Build()
   {
      mRows.clear();
      wxMenu *menu = mStack.back().menu;
      int y = 2;
      if (mStack.size() > 1) {
         Row back;
         back.back = true;
         back.label = mStack.back().title;
         back.top = y;
         back.height = kRowHeight + 2;
         mRows.push_back(back);
         y += back.height;
         Row sep;
         sep.separator = true;
         sep.top = y;
         sep.height = kSeparatorHeight;
         mRows.push_back(sep);
         y += sep.height;
      }
      for (auto *item : menu->GetMenuItems()) {
         Row row;
         row.item = item;
         row.top = y;
         if (item->IsSeparator()) {
            row.separator = true;
            row.height = kSeparatorHeight;
         }
         else {
            SplitLabel(*item, row.label, row.shortcut);
            row.enabled = item->IsEnabled();
            row.checkable = item->IsCheckable();
            row.checked = row.checkable && item->IsChecked();
            row.submenu = item->IsSubMenu();
            row.height = kRowHeight;
         }
         y += row.height;
         mRows.push_back(row);
      }
      mContentHeight = y + 2;

      wxClientDC dc(this);
      dc.SetFont(GetFont());
      int labelWidth = 80, shortcutWidth = 0;
      for (const auto &row : mRows) {
         if (row.separator)
            continue;
         labelWidth = std::max(labelWidth, dc.GetTextExtent(row.label).x);
         shortcutWidth = std::max(shortcutWidth, dc.GetTextExtent(row.shortcut).x);
      }
      const int width = kPadX * 2 + 18 + labelWidth +
         (shortcutWidth ? shortcutWidth + 24 : 0) + 18;
      int maxHeight = 600;
      const int display = wxDisplay::GetFromWindow(mOwner);
      if (display != wxNOT_FOUND)
         maxHeight = wxDisplay(display).GetClientArea().height - 80;
      const int height = std::min(mContentHeight, maxHeight);
      mScroll = 0;
      mHover = -1;
      SetSize(wxSize(width, height));
      // Pack: the first selectable row gets keyboard focus on open.
      Refresh(false);
   }

   int RowAt(const wxPoint &point) const
   {
      const int y = point.y + mScroll;
      for (size_t i = 0; i < mRows.size(); ++i)
         if (y >= mRows[i].top && y < mRows[i].top + mRows[i].height &&
             !mRows[i].separator)
            return static_cast<int>(i);
      return -1;
   }

   void Choose(int index)
   {
      if (index < 0 || index >= static_cast<int>(mRows.size()))
         return;
      const auto &row = mRows[index];
      if (row.back) {
         mStack.pop_back();
         Build();
         return;
      }
      if (!row.item || !row.enabled)
         return;
      if (row.submenu) {
         mStack.push_back({ row.item->GetSubMenu(), row.label });
         Build();
         return;
      }
      mActiveMenu = mStack.back().menu;
      mActiveId = row.item->GetId();
      DismissAndNotify();
   }

   void Move(int direction)
   {
      if (mRows.empty())
         return;
      int index = mHover;
      for (size_t n = 0; n < mRows.size(); ++n) {
         index = (index + direction + static_cast<int>(mRows.size())) %
            static_cast<int>(mRows.size());
         const auto &row = mRows[index];
         if (!row.separator && (row.enabled || row.back)) {
            mHover = index;
            EnsureVisible(index);
            Refresh(false);
            return;
         }
      }
   }

   void EnsureVisible(int index)
   {
      const auto &row = mRows[index];
      const int viewHeight = GetClientSize().y;
      if (row.top < mScroll)
         mScroll = std::max(0, row.top - 2);
      else if (row.top + row.height > mScroll + viewHeight)
         mScroll = std::min(
            std::max(0, mContentHeight - viewHeight),
            row.top + row.height - viewHeight + 2);
   }

   void OnPaint(wxPaintEvent &)
   {
      wxPaintDC dc(this);
      const auto size = GetClientSize();
      if (size.x <= 0 || size.y <= 0)
         return;
      dc.SetFont(GetFont());
      AlicornScene scene;
      scene.Fill(wxRect(0, 0, size.x, size.y), mPalette.background);
      const auto font = GetFont();
      struct Text { wxRect bounds; wxString text; wxColour colour; };
      for (size_t i = 0; i < mRows.size(); ++i) {
         const auto &row = mRows[i];
         const int top = row.top - mScroll;
         if (top + row.height < 0 || top > size.y)
            continue;
         if (row.separator) {
            scene.Fill(
               wxRect(kPadX, top + row.height / 2, size.x - 2 * kPadX, 1),
               mPalette.line);
            continue;
         }
         const bool hovered = static_cast<int>(i) == mHover &&
            (row.enabled || row.back);
         if (hovered)
            scene.Fill(wxRect(2, top, size.x - 4, row.height), mPalette.highlight);
         const auto colour = hovered ? mPalette.highlightText
            : (row.enabled || row.back) ? mPalette.text : mPalette.disabled;
         const int textHeight = dc.GetTextExtent(row.label.empty() ? wxString("0") : row.label).y;
         const int textTop = top + (row.height - textHeight) / 2;
         if (row.back) {
            scene.Line({ kPadX + 6, top + row.height / 2 - 4 },
                       { kPadX + 2, top + row.height / 2 }, colour, 1.5f);
            scene.Line({ kPadX + 2, top + row.height / 2 },
                       { kPadX + 6, top + row.height / 2 + 4 }, colour, 1.5f);
         }
         if (row.checked) {
            const int cx = kPadX + 4, cy = top + row.height / 2;
            scene.Line({ cx - 3, cy }, { cx - 1, cy + 3 }, colour, 1.5f);
            scene.Line({ cx - 1, cy + 3 }, { cx + 4, cy - 3 }, colour, 1.5f);
         }
         scene.Text(
            wxRect(kPadX + 18, textTop, std::max(1, size.x - kPadX * 2 - 18), textHeight),
            row.label, colour, font);
         if (!row.shortcut.empty()) {
            const int w = dc.GetTextExtent(row.shortcut).x;
            scene.Text(
               wxRect(size.x - kPadX - 18 - w, textTop, w + 2, textHeight),
               row.shortcut, hovered ? colour : mPalette.disabled, font);
         }
         if (row.submenu) {
            const int cx = size.x - kPadX - 4, cy = top + row.height / 2;
            scene.Line({ cx - 3, cy - 4 }, { cx + 1, cy }, colour, 1.5f);
            scene.Line({ cx + 1, cy }, { cx - 3, cy + 4 }, colour, 1.5f);
         }
      }
      if (!mScene.Draw(dc, wxRect(0, 0, size.x, size.y), scene)) {
         dc.SetBackground(wxBrush(mPalette.background));
         dc.Clear();
         dc.SetTextForeground(mPalette.text);
         for (const auto &row : mRows)
            if (!row.separator)
               dc.DrawText(row.label, kPadX + 18, row.top - mScroll + 3);
      }
   }

   void OnMotion(wxMouseEvent &event)
   {
      const int index = RowAt(event.GetPosition());
      if (index != mHover) {
         mHover = index;
         Refresh(false);
      }
   }

   void OnLeftUp(wxMouseEvent &event)
   {
      Choose(RowAt(event.GetPosition()));
   }

   void OnWheel(wxMouseEvent &event)
   {
      const int viewHeight = GetClientSize().y;
      if (mContentHeight <= viewHeight)
         return;
      const int delta = event.GetWheelRotation() > 0 ? -kRowHeight * 2 : kRowHeight * 2;
      mScroll = std::clamp(mScroll + delta, 0, mContentHeight - viewHeight);
      Refresh(false);
   }

   void OnKey(wxKeyEvent &event)
   {
      switch (event.GetKeyCode()) {
      case WXK_ESCAPE:
         if (mStack.size() > 1) {
            mStack.pop_back();
            Build();
         }
         else
            DismissAndNotify();
         return;
      case WXK_DOWN: case WXK_NUMPAD_DOWN: Move(1); return;
      case WXK_UP: case WXK_NUMPAD_UP: Move(-1); return;
      case WXK_RETURN: case WXK_NUMPAD_ENTER: case WXK_SPACE: case WXK_RIGHT:
         Choose(mHover); return;
      case WXK_LEFT: case WXK_BACK:
         if (mStack.size() > 1) {
            mStack.pop_back();
            Build();
         }
         return;
      default:
         event.Skip();
      }
   }

   AlicornMenuBar *mOwner;
   AlicornSceneView mScene;
   Palette mPalette;
   std::vector<Level> mStack;
   std::vector<Row> mRows;
   int mContentHeight{};
   int mScroll{};
   int mHover{ -1 };
   wxMenu *mActiveMenu{};
   int mActiveId{};
};

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
   auto *popup = safenew AlicornMenuPopup(this, menu, titles[index].label);
   popup->ShowAt(ClientToScreen(wxPoint(titles[index].bounds.x, kBarHeight)));
   Refresh(false);
}

void AlicornMenuBar::PopupClosed()
{
   mOpenIndex = -1;
   Refresh(false);
}

void AlicornMenuBar::Activate(wxMenu *menu, int itemId)
{
   auto *item = menu->FindItem(itemId);
   if (!item || !mFrame)
      return;
   if (item->IsCheckable())
      item->Check(!item->IsChecked());
   wxCommandEvent event(wxEVT_MENU, itemId);
   event.SetEventObject(menu);
   if (item->IsCheckable())
      event.SetInt(item->IsChecked() ? 1 : 0);
   mFrame->GetEventHandler()->ProcessEvent(event);
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
