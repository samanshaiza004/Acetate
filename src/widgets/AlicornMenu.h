/*  SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#ifdef AUDACITY_USE_ALICORN_SNAP_UI

#include <functional>
#include <memory>
#include <vector>

#include <wx/gdicmn.h>
#include <wx/string.h>

class wxMenu;
class wxWindow;

// A menu presented through a retained Alicorn scene without blocking.
//
// The caller describes the menu as plain data. The session owns that data, the
// selection and the keyboard state, and runs the chosen item's action after
// the menu has closed, so nothing borrows the caller's stack. Actions must
// capture only values or weak references and re-validate them when run.
namespace AlicornMenu {

struct Item {
   wxString label;       // display text, mnemonics already removed
   wxString shortcut;    // right-aligned accelerator text, may be empty
   bool separator{};
   bool enabled{ true };
   bool checkable{};
   bool checked{};
   std::vector<Item> children;   // non-empty for a submenu
   std::function<void()> action; // run after the menu closed
};

using Model = std::vector<Item>;

struct Options {
   // Heading used by the back row when a submenu is entered.
   wxString title;
   // Left/Right at the top level: +1 next, -1 previous. Used by menu bars.
   std::function<void(int direction)> onSwitch;
   // Called once when the session ends; `chosen` tells whether an action ran.
   std::function<void(bool chosen)> onClosed;
   // Window to refocus when the menu closes; defaults to the window that had
   // focus when the menu opened.
   wxWindow *restoreFocus{};
   // Record the selection in, and replay it from, the journal.
   bool journal{ true };
   // Kept alive until the session has finished, for data the actions use.
   std::shared_ptr<void> keepAlive;
   // Called last: after the chosen action has run, or after dismissal.
   std::function<void()> onFinished;
};

// Opens the menu with its top-left corner at `screenPos` and returns at once.
void Show(wxWindow &owner, const wxPoint &screenPos, Model model,
          Options options = {});

// For callers that already build a wxMenu with bound handlers. The session
// takes ownership of the menu (nothing is borrowed from the caller's stack),
// dispatches the chosen item exactly as wxMenu::PopupMenu would — through the
// menu, its parents and `window` — and sends wxEVT_MENU_CLOSE when it closes.
// Handlers must be safe if `window` has been destroyed: nothing is dispatched
// once it is gone. `clientPos` is relative to `window`.
void PopupOwned(wxWindow &window, std::unique_ptr<wxMenu> menu,
                const wxPoint &clientPos, Options options = {});

// Snapshot of a wx menu's structure and state. `activate` receives the item id
// when an item is chosen and must resolve it again at that time.
Model ModelFromMenu(wxMenu &menu, const std::function<void(int id)> &activate);

}

#endif
