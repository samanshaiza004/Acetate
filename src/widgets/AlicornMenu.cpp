/*  SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef AUDACITY_USE_ALICORN_SNAP_UI

#include "AlicornMenu.h"

#include <algorithm>
#include <memory>

#include <wx/dcclient.h>
#include <wx/display.h>
#include <wx/menu.h>
#include <wx/popupwin.h>
#include <wx/settings.h>
#include <wx/weakref.h>

#include <wx/arrstr.h>
#include <wx/wxcrt.h>

#include "Journal.h"
#include "JournalEvents.h"
#include "JournalOutput.h"
#include "wxArrayStringEx.h"
#include "../alicorn/AlicornTimelineView.h"
#include "../tracks/ui/RetainedTrackControls.h"

namespace AlicornMenu {
namespace {
constexpr int kRowHeight = 22;
constexpr int kSeparatorHeight = 7;
constexpr int kPadX = 10;
const auto JournalCode = L"PopupMenu";

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
   const Item *item{};   // null for the back header
   bool separator{};
   bool back{};
   int top{};
   int height{};
   bool selectable() const { return back || (item && !separator && item->enabled); }
};

// Path of labels leading to `target`, only when each is unique among its
// siblings, as the journal requires.
bool FindPath(
   const Model &items, const Item *target, wxArrayStringEx &path, bool &unique)
{
   for (const auto &item : items) {
      if (&item == target) {
         path.push_back(item.label);
         unique = unique && 1 == std::count_if(items.begin(), items.end(),
            [&](const Item &other) { return other.label == item.label; });
         return true;
      }
      if (!item.children.empty()) {
         wxArrayStringEx sub;
         if (FindPath(item.children, target, sub, unique)) {
            path.push_back(item.label);
            for (const auto &name : sub)
               path.push_back(name);
            unique = unique && 1 == std::count_if(items.begin(), items.end(),
               [&](const Item &other) { return other.label == item.label; });
            return true;
         }
      }
   }
   return false;
}

const Item *FindByPath(const Model &root, const wxArrayStringEx &names, size_t first)
{
   const Model *level = &root;
   const Item *found = nullptr;
   for (size_t i = first; i < names.size(); ++i) {
      const auto &name = names[i];
      const Item *match = nullptr;
      int count = 0;
      for (const auto &item : *level)
         if (!item.separator && item.label == name) {
            match = &item;
            ++count;
         }
      if (count != 1)
         return nullptr;
      found = match;
      level = &match->children;
   }
   return found;
}
}

class MenuPopup final : public wxPopupTransientWindow {
public:
   MenuPopup(wxWindow *owner, Model model, Options options)
      : wxPopupTransientWindow(owner, wxBORDER_SIMPLE)
      , mModel{ std::move(model) }
      , mOptions{ std::move(options) }
      , mPalette{ MakePalette() }
      , mOwner{ owner }
   {
      SetBackgroundStyle(wxBG_STYLE_PAINT);
      if (!mOptions.restoreFocus)
         mOptions.restoreFocus = wxWindow::FindFocus();
      mRestore = mOptions.restoreFocus;
      mStack.push_back({ &mModel, mOptions.title });
      Build();
      Bind(wxEVT_PAINT, &MenuPopup::OnPaint, this);
      Bind(wxEVT_MOTION, &MenuPopup::OnMotion, this);
      Bind(wxEVT_LEFT_UP, &MenuPopup::OnLeftUp, this);
      Bind(wxEVT_MOUSEWHEEL, &MenuPopup::OnWheel, this);
      Bind(wxEVT_CHAR_HOOK, &MenuPopup::OnKey, this);
   }

   void ShowAt(const wxPoint &screen)
   {
      Position(screen, wxSize(0, 0));
      wxPopupTransientWindow::Popup();
   }

   // Selected path for journaling, filled when an action is chosen.
   const Model &RootModel() const { return mModel; }

protected:
   void OnDismiss() override
   {
      wxPopupTransientWindow::OnDismiss();
      // Everything the deferred step needs is moved out; the popup is then
      // destroyed, so no session state outlives the menu.
      auto action = std::move(mChosenAction);
      auto onSwitch = std::move(mOptions.onSwitch);
      auto onClosed = std::move(mOptions.onClosed);
      auto onFinished = std::move(mOptions.onFinished);
      auto keepAlive = mOptions.keepAlive;
      const int direction = mSwitch;
      const bool chosen = static_cast<bool>(action);
      const bool journal = mOptions.journal;
      wxArrayStringEx path = mChosenPath;
      const bool pathOk = mChosenPathUnique;
      wxWeakRef<wxWindow> restore = mRestore;
      wxWindow *owner = mOwner;
      owner->CallAfter([=, action = std::move(action)]() mutable {
         if (restore)
            restore->SetFocus();
         if (onClosed)
            onClosed(chosen);
         if (journal) {
            if (chosen) {
               if (Journal::Events::IsWatching()) {
                  if (pathOk) {
                     path.insert(path.begin(), JournalCode);
                     Journal::Output(path);
                  }
                  else
                     Journal::Events::FailedEventSerialization();
               }
            }
            else
               Journal::Output(JournalCode);
         }
         if (chosen)
            action();
         else if (direction != 0 && onSwitch)
            onSwitch(direction);
         if (onFinished)
            onFinished();
         (void)keepAlive;
      });
      CallAfter([this] { Destroy(); });
   }

private:
   struct Level {
      const Model *items;
      wxString title;
   };

   void Build()
   {
      mRows.clear();
      const Model &items = *mStack.back().items;
      int y = 2;
      if (mStack.size() > 1) {
         Row back;
         back.back = true;
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
      for (const auto &item : items) {
         Row row;
         row.item = &item;
         row.top = y;
         row.separator = item.separator;
         row.height = item.separator ? kSeparatorHeight : kRowHeight;
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
         labelWidth = std::max(labelWidth, dc.GetTextExtent(LabelOf(row)).x);
         if (row.item)
            shortcutWidth = std::max(shortcutWidth, dc.GetTextExtent(row.item->shortcut).x);
      }
      const int width = kPadX * 2 + 18 + labelWidth +
         (shortcutWidth ? shortcutWidth + 24 : 0) + 18;
      int maxHeight = 600;
      const int display = wxDisplay::GetFromWindow(mOwner);
      if (display != wxNOT_FOUND)
         maxHeight = wxDisplay(display).GetClientArea().height - 80;
      mScroll = 0;
      mHover = -1;
      SetSize(wxSize(width, std::min(mContentHeight, maxHeight)));
      Refresh(false);
   }

   wxString LabelOf(const Row &row) const
   {
      return row.back ? mStack.back().title : row.item->label;
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

   void Back()
   {
      mStack.pop_back();
      Build();
   }

   void Choose(int index)
   {
      if (index < 0 || index >= static_cast<int>(mRows.size()))
         return;
      const auto &row = mRows[index];
      if (row.back) {
         Back();
         return;
      }
      if (!row.item || !row.selectable())
         return;
      if (!row.item->children.empty()) {
         mStack.push_back({ &row.item->children, row.item->label });
         Build();
         return;
      }
      if (!row.item->action)
         return;
      mChosenAction = row.item->action;
      mChosenPathUnique = true;
      FindPath(mModel, row.item, mChosenPath, mChosenPathUnique);
      DismissAndNotify();
   }

   void Move(int direction)
   {
      if (mRows.empty())
         return;
      int index = mHover < 0 && direction < 0 ? 0 : mHover;
      for (size_t n = 0; n < mRows.size(); ++n) {
         index = (index + direction + static_cast<int>(mRows.size())) %
            static_cast<int>(mRows.size());
         if (mRows[index].selectable()) {
            mHover = index;
            EnsureVisible(index);
            Refresh(false);
            return;
         }
      }
   }

   void JumpToLetter(wxChar letter)
   {
      const wxString key = wxString(letter).Lower();
      const int count = static_cast<int>(mRows.size());
      for (int n = 1; n <= count; ++n) {
         const int index = (std::max(mHover, -1) + n + count) % count;
         const auto &row = mRows[index];
         if (row.selectable() && !row.back &&
             row.item->label.Lower().StartsWith(key)) {
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
      const auto font = GetFont();
      AlicornScene scene;
      scene.Fill(wxRect(0, 0, size.x, size.y), mPalette.background);
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
         const bool hovered = static_cast<int>(i) == mHover && row.selectable();
         if (hovered)
            scene.Fill(wxRect(2, top, size.x - 4, row.height), mPalette.highlight);
         const auto colour = hovered ? mPalette.highlightText
            : row.selectable() ? mPalette.text : mPalette.disabled;
         const wxString label = LabelOf(row);
         const int textHeight = dc.GetTextExtent(label.empty() ? wxString("0") : label).y;
         const int textTop = top + (row.height - textHeight) / 2;
         if (row.back) {
            scene.Line({ kPadX + 6, top + row.height / 2 - 4 },
                       { kPadX + 2, top + row.height / 2 }, colour, 1.5f);
            scene.Line({ kPadX + 2, top + row.height / 2 },
                       { kPadX + 6, top + row.height / 2 + 4 }, colour, 1.5f);
         }
         else if (row.item->checkable && row.item->checked) {
            const int cx = kPadX + 4, cy = top + row.height / 2;
            scene.Line({ cx - 3, cy }, { cx - 1, cy + 3 }, colour, 1.5f);
            scene.Line({ cx - 1, cy + 3 }, { cx + 4, cy - 3 }, colour, 1.5f);
         }
         scene.Text(
            wxRect(kPadX + 18, textTop, std::max(1, size.x - kPadX * 2 - 18), textHeight),
            label, colour, font);
         if (row.item && !row.item->shortcut.empty()) {
            const int w = dc.GetTextExtent(row.item->shortcut).x;
            scene.Text(
               wxRect(size.x - kPadX - 18 - w, textTop, w + 2, textHeight),
               row.item->shortcut, hovered ? colour : mPalette.disabled, font);
         }
         if (row.item && !row.item->children.empty()) {
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
               dc.DrawText(LabelOf(row), kPadX + 18, row.top - mScroll + 3);
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

   void OnLeftUp(wxMouseEvent &event) { Choose(RowAt(event.GetPosition())); }

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
      if (event.HasAnyModifiers() && !event.ShiftDown()) {
         event.Skip();
         return;
      }
      switch (event.GetKeyCode()) {
      case WXK_ESCAPE:
         if (mStack.size() > 1)
            Back();
         else
            DismissAndNotify();
         return;
      case WXK_DOWN: case WXK_NUMPAD_DOWN: Move(1); return;
      case WXK_UP: case WXK_NUMPAD_UP: Move(-1); return;
      case WXK_HOME: mHover = -1; Move(1); return;
      case WXK_END: mHover = 0; Move(-1); return;
      case WXK_RETURN: case WXK_NUMPAD_ENTER: case WXK_SPACE:
         Choose(mHover); return;
      case WXK_RIGHT: case WXK_NUMPAD_RIGHT:
         if (mHover >= 0 && mRows[mHover].item &&
             !mRows[mHover].item->children.empty())
            Choose(mHover);
         else if (mStack.size() == 1 && mOptions.onSwitch) {
            mSwitch = 1;
            DismissAndNotify();
         }
         return;
      case WXK_LEFT: case WXK_NUMPAD_LEFT: case WXK_BACK:
         if (mStack.size() > 1)
            Back();
         else if (mOptions.onSwitch && event.GetKeyCode() != WXK_BACK) {
            mSwitch = -1;
            DismissAndNotify();
         }
         return;
      default:
         break;
      }
      const wxChar ch = event.GetUnicodeKey();
      if (ch >= 32 && ch != WXK_DELETE && wxIsalnum(ch)) {
         JumpToLetter(ch);
         return;
      }
      event.Skip();
   }

   Model mModel;
   Options mOptions;
   AlicornSceneView mScene;
   Palette mPalette;
   wxWindow *mOwner;
   wxWeakRef<wxWindow> mRestore;
   std::vector<Level> mStack;
   std::vector<Row> mRows;
   int mContentHeight{};
   int mScroll{};
   int mHover{ -1 };
   int mSwitch{};
   std::function<void()> mChosenAction;
   wxArrayStringEx mChosenPath;
   bool mChosenPathUnique{ true };
};

void Show(wxWindow &owner, const wxPoint &screenPos, Model model, Options options)
{
   if (options.journal && Journal::IsReplaying()) {
      // Do not show anything: find the recorded item and run it now, with the
      // same failure behaviour as BasicMenu's replay.
      const auto fields = Journal::GetTokens();
      if (!fields.empty() && fields[0] == JournalCode) {
         if (fields.size() == 1) {
            if (options.onClosed)
               options.onClosed(false);
            if (options.onFinished)
               options.onFinished();
            return;
         }
         if (const auto *item = FindByPath(model, fields, 1);
             item && item->action) {
            if (options.onClosed)
               options.onClosed(true);
            item->action();
            if (options.onFinished)
               options.onFinished();
            return;
         }
      }
      throw Journal::SyncException(wxString::Format(
         "PopupMenu has failed to invoke %s",
         wxJoin(fields, ',').ToStdString().c_str()));
   }
   auto *popup = safenew MenuPopup(&owner, std::move(model), std::move(options));
   popup->ShowAt(screenPos);
}

void PopupOwned(wxWindow &window, std::unique_ptr<wxMenu> menu,
                const wxPoint &clientPos, Options options)
{
   std::shared_ptr<wxMenu> owned{ std::move(menu) };
   wxWeakRef<wxWindow> windowRef{ &window };
   owned->SetInvokingWindow(&window);
   auto model = ModelFromMenu(*owned, [owned, windowRef](int id) {
      if (!windowRef)
         return;
      wxMenu *container = nullptr;
      auto *item = owned->FindItem(id, &container);
      if (!item || !container)
         return;
      // Radio items stay selected; check items toggle, as natively.
      if (item->GetKind() == wxITEM_CHECK)
         item->Check(!item->IsChecked());
      else if (item->GetKind() == wxITEM_RADIO)
         item->Check(true);
      container->SendEvent(id, item->IsCheckable() ? (item->IsChecked() ? 1 : 0) : -1);
   });
   const auto previousClosed = std::move(options.onClosed);
   options.onClosed = [owned, windowRef, previousClosed](bool chosen) {
      if (windowRef) {
         wxMenuEvent event(wxEVT_MENU_CLOSE, -1, owned.get());
         event.SetEventObject(owned.get());
         wxMenuBase::ProcessMenuEvent(owned.get(), event, windowRef.get());
      }
      if (previousClosed)
         previousClosed(chosen);
   };
   Show(window, window.ClientToScreen(clientPos), std::move(model), std::move(options));
}

Model ModelFromMenu(wxMenu &menu, const std::function<void(int id)> &activate)
{
   Model model;
   for (auto *item : menu.GetMenuItems()) {
      Item entry;
      if (item->IsSeparator()) {
         entry.separator = true;
         model.push_back(std::move(entry));
         continue;
      }
      wxString full = item->GetItemLabel();
      const auto tab = full.Find('\t');
      if (tab != wxNOT_FOUND) {
         entry.shortcut = full.Mid(tab + 1);
         full = full.Left(tab);
      }
      entry.label = wxStripMenuCodes(full);
      entry.enabled = item->IsEnabled();
      entry.checkable = item->IsCheckable();
      entry.checked = entry.checkable && item->IsChecked();
      if (item->IsSubMenu()) {
         entry.children = ModelFromMenu(*item->GetSubMenu(), activate);
      }
      else {
         const int id = item->GetId();
         entry.action = [activate, id] { activate(id); };
      }
      model.push_back(std::move(entry));
   }
   return model;
}

}

#endif
