/*  SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef AUDACITY_USE_ALICORN_SNAP_UI

#include "AlicornFields.h"

#include <algorithm>
#include <cmath>

#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/popupwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>

#include "../toolbars/AlicornActionControl.h"
#include "../tracks/ui/RetainedTrackControls.h"

namespace {
constexpr int kHeight = 24;
constexpr int kStepperWidth = 16;
constexpr int kTextPad = 4;

using RetainedTrackControls::Mix;

struct FieldColours {
   wxColour background, text, border, highlight, highlightText, focusRing;
};

FieldColours Colours(bool enabled)
{
   FieldColours c;
   c.background = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
   c.text = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
   if (!enabled)
      c.text = Mix(c.text, c.background, 0.55);
   c.border = Mix(c.background, *wxBLACK, 0.35);
   c.highlight = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT);
   c.highlightText = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT);
   c.focusRing = c.highlight;
   return c;
}

void Frame(AlicornScene &scene, const wxRect &r, const FieldColours &c, bool focused)
{
   scene.Fill(r, focused ? c.focusRing : c.border);
   scene.Fill(wxRect(r).Deflate(1), c.background);
}

void ChevronUp(AlicornScene &s, const wxRect &r, const wxColour &colour)
{
   const int cx = r.x + r.width / 2, cy = r.y + r.height / 2;
   s.Line({ cx - 3, cy + 1 }, { cx, cy - 2 }, colour, 1.5f);
   s.Line({ cx, cy - 2 }, { cx + 3, cy + 1 }, colour, 1.5f);
}

void ChevronDown(AlicornScene &s, const wxRect &r, const wxColour &colour)
{
   const int cx = r.x + r.width / 2, cy = r.y + r.height / 2;
   s.Line({ cx - 3, cy - 1 }, { cx, cy + 2 }, colour, 1.5f);
   s.Line({ cx, cy + 2 }, { cx + 3, cy - 1 }, colour, 1.5f);
}
}

// --------------------------------------------------------------------------
// AlicornSpinField

AlicornSpinField::AlicornSpinField(
   wxWindow *parent, wxWindowID id, double value, double min, double max,
   double step, bool allowFractional, const wxPoint &pos, const wxSize &size)
   : wxControl(parent, id, pos, size, wxBORDER_NONE | wxWANTS_CHARS)
   , mMin{ std::min(min, max) }
   , mMax{ std::max(min, max) }
   , mStep{ step }
   , mFractional{ allowFractional }
{
   SetBackgroundStyle(wxBG_STYLE_PAINT);
   SetInitialSize(size);
   mValue = std::clamp(value, mMin, mMax);
   mBuffer = FormatValue();
   Bind(wxEVT_PAINT, &AlicornSpinField::OnPaint, this);
   Bind(wxEVT_CHAR_HOOK, &AlicornSpinField::OnCharHook, this);
   Bind(wxEVT_LEFT_DOWN, &AlicornSpinField::OnLeftDown, this);
   Bind(wxEVT_MOUSEWHEEL, &AlicornSpinField::OnWheel, this);
   Bind(wxEVT_SET_FOCUS, &AlicornSpinField::OnSetFocus, this);
   Bind(wxEVT_KILL_FOCUS, &AlicornSpinField::OnKillFocus, this);
}

wxSize AlicornSpinField::DoGetBestSize() const
{
   return wxSize(60, kHeight);
}

void AlicornSpinField::SetName(const TranslatableString &name)
{
   wxControl::SetName(name.Translation());
}

bool AlicornSpinField::Enable(bool enable)
{
   const bool result = wxControl::Enable(enable);
   Refresh(false);
   return result;
}

wxString AlicornSpinField::FormatValue() const
{
   return wxString::FromDouble(mValue);
}

void AlicornSpinField::SetValue(double value)
{
   SetValueInternal(value, false);
}

void AlicornSpinField::SetValueInternal(double value, bool notify)
{
   value = std::clamp(value, mMin, mMax);
   if (!mFractional)
      value = std::round(value);
   const bool changed = value != mValue;
   mValue = value;
   mBuffer = FormatValue();
   mCaret = static_cast<int>(mBuffer.length());
   if (mEditing)
      mSelectAll = true;
   Refresh(false);
   if (changed && notify) {
      wxCommandEvent event(wxEVT_SPINCTRL, GetId());
      event.SetEventObject(this);
      ProcessWindowEvent(event);
   }
}

void AlicornSpinField::Commit()
{
   double parsed;
   if (mBuffer.ToDouble(&parsed))
      SetValueInternal(parsed, true);
   else
      SetValueInternal(mValue, false);
}

void AlicornSpinField::Step(double direction)
{
   SetValueInternal(mValue + direction * mStep, true);
}

void AlicornSpinField::InsertText(const wxString &text)
{
   if (mSelectAll) {
      mBuffer.clear();
      mCaret = 0;
      mSelectAll = false;
   }
   if (mBuffer.length() + text.length() > 10)
      return;
   mBuffer.insert(mCaret, text);
   mCaret += static_cast<int>(text.length());
}

wxRect AlicornSpinField::StepperRect() const
{
   const auto size = GetClientSize();
   return wxRect(size.x - kStepperWidth, 0, kStepperWidth, size.y);
}

int AlicornSpinField::CaretFromX(int x) const
{
   wxClientDC dc(const_cast<AlicornSpinField *>(this));
   dc.SetFont(GetFont());
   const int rel = x - kTextPad;
   for (int i = 0; i < static_cast<int>(mBuffer.length()); ++i) {
      const int left = dc.GetTextExtent(mBuffer.Left(i)).x;
      const int right = dc.GetTextExtent(mBuffer.Left(i + 1)).x;
      if (rel < (left + right) / 2)
         return i;
   }
   return static_cast<int>(mBuffer.length());
}

void AlicornSpinField::OnPaint(wxPaintEvent &)
{
   wxPaintDC dc(this);
   const auto size = GetClientSize();
   if (size.x <= 0 || size.y <= 0)
      return;
   const bool enabled = IsEnabled();
   const bool focused = enabled && HasFocus();
   const auto colours = Colours(enabled);
   const auto font = GetFont();
   dc.SetFont(font);
   const wxString text = mEditing ? mBuffer : FormatValue();
   const auto extent = dc.GetTextExtent(text.empty() ? wxString("0") : text);

   AlicornScene scene;
   Frame(scene, wxRect(0, 0, size.x, size.y), colours, focused);
   const auto stepper = StepperRect();
   scene.Fill(wxRect(stepper.x, 1, 1, size.y - 2), colours.border);
   ChevronUp(scene, wxRect(stepper.x, 0, stepper.width, size.y / 2), colours.text);
   ChevronDown(
      scene, wxRect(stepper.x, size.y / 2, stepper.width, size.y - size.y / 2),
      colours.text);

   const int textTop = (size.y - extent.y) / 2;
   wxColour textColour = colours.text;
   if (mEditing && mSelectAll && !text.empty()) {
      scene.Fill(wxRect(kTextPad, textTop, extent.x, extent.y), colours.highlight);
      textColour = colours.highlightText;
   }
   scene.Text(
      wxRect(kTextPad, textTop, std::max(1, stepper.x - kTextPad - 2), extent.y),
      text, textColour, font);
   if (focused && !mSelectAll) {
      const int caretX = kTextPad + dc.GetTextExtent(text.Left(mCaret)).x;
      scene.Fill(wxRect(caretX, textTop, 1, extent.y), colours.text);
   }

   if (!mScene.Draw(dc, wxRect(0, 0, size.x, size.y), scene)) {
      dc.SetBackground(wxBrush(colours.background));
      dc.Clear();
      dc.SetPen(wxPen(colours.border));
      dc.SetBrush(*wxTRANSPARENT_BRUSH);
      dc.DrawRectangle(0, 0, size.x, size.y);
      dc.SetTextForeground(colours.text);
      dc.DrawText(text, kTextPad, textTop);
   }
}

void AlicornSpinField::OnCharHook(wxKeyEvent &event)
{
   if (!HasFocus()) {
      event.Skip();
      return;
   }
   const int key = event.GetKeyCode();
   const bool command = event.ControlDown() || event.AltDown() || event.MetaDown();
   if (command) {
      if ((key == 'A' || key == 'a') && !event.AltDown()) {
         mSelectAll = true;
         Refresh(false);
         return;
      }
      event.Skip();
      return;
   }
   const bool shift = event.ShiftDown();
   switch (key) {
   case WXK_UP: case WXK_NUMPAD_UP:
      Step(shift ? 10.0 : 1.0); return;
   case WXK_DOWN: case WXK_NUMPAD_DOWN:
      Step(shift ? -10.0 : -1.0); return;
   case WXK_PAGEUP: case WXK_NUMPAD_PAGEUP:
      Step(10.0); return;
   case WXK_PAGEDOWN: case WXK_NUMPAD_PAGEDOWN:
      Step(-10.0); return;
   case WXK_RETURN: case WXK_NUMPAD_ENTER:
      Commit(); return;
   case WXK_ESCAPE:
      mBuffer = FormatValue();
      mSelectAll = true;
      mCaret = static_cast<int>(mBuffer.length());
      Refresh(false);
      return;
   case WXK_LEFT:
      mCaret = mSelectAll ? 0 : std::max(0, mCaret - 1);
      mSelectAll = false; Refresh(false); return;
   case WXK_RIGHT:
      mCaret = mSelectAll ? static_cast<int>(mBuffer.length())
                          : std::min<int>(mBuffer.length(), mCaret + 1);
      mSelectAll = false; Refresh(false); return;
   case WXK_HOME:
      mCaret = 0; mSelectAll = false; Refresh(false); return;
   case WXK_END:
      mCaret = static_cast<int>(mBuffer.length()); mSelectAll = false;
      Refresh(false); return;
   case WXK_BACK:
      if (mSelectAll) {
         mBuffer.clear(); mCaret = 0; mSelectAll = false;
      }
      else if (mCaret > 0) {
         mBuffer.erase(mCaret - 1, 1); --mCaret;
      }
      Refresh(false); return;
   case WXK_DELETE:
      if (mSelectAll) {
         mBuffer.clear(); mCaret = 0; mSelectAll = false;
      }
      else if (mCaret < static_cast<int>(mBuffer.length())) {
         mBuffer.erase(mCaret, 1);
      }
      Refresh(false); return;
   case WXK_TAB:
      event.Skip(); return;
   default:
      break;
   }

   const wxChar ch = event.GetUnicodeKey();
   if (ch >= '0' && ch <= '9') {
      InsertText(wxString(ch));
      Refresh(false);
      return;
   }
   if (ch == '.' && mFractional) {
      const bool replacing = mSelectAll;
      if (replacing || mBuffer.Find('.') == wxNOT_FOUND) {
         InsertText(".");
         Refresh(false);
      }
      return;
   }
   if (ch == '-' && mMin < 0) {
      if (mSelectAll || (mCaret == 0 && !mBuffer.StartsWith("-"))) {
         InsertText("-");
         Refresh(false);
      }
      return;
   }
   if (key >= WXK_SPACE && key < WXK_START) {
      // Other printable characters are not valid in a number.
      return;
   }
   event.Skip();
}

void AlicornSpinField::OnLeftDown(wxMouseEvent &event)
{
   if (!IsEnabled())
      return;
   const auto pos = event.GetPosition();
   if (StepperRect().Contains(pos)) {
      if (!HasFocus())
         SetFocus();
      Step(pos.y < GetClientSize().y / 2 ? 1.0 : -1.0);
      return;
   }
   if (!HasFocus()) {
      SetFocus();
      return;
   }
   mCaret = CaretFromX(pos.x);
   mSelectAll = false;
   Refresh(false);
}

void AlicornSpinField::OnWheel(wxMouseEvent &event)
{
   if (!IsEnabled())
      return;
   const auto delta = event.GetWheelDelta();
   const auto rotation = event.GetWheelRotation();
   if (rotation >= delta)
      Step(event.ShiftDown() ? 10.0 : 1.0);
   else if (rotation <= -delta)
      Step(event.ShiftDown() ? -10.0 : -1.0);
}

void AlicornSpinField::OnSetFocus(wxFocusEvent &event)
{
   mEditing = true;
   mBuffer = FormatValue();
   mCaret = static_cast<int>(mBuffer.length());
   mSelectAll = true;
   Refresh(false);
   event.Skip();
}

void AlicornSpinField::OnKillFocus(wxFocusEvent &event)
{
   Commit();
   mEditing = false;
   mSelectAll = false;
   Refresh(false);
   event.Skip();
}

// --------------------------------------------------------------------------
// AlicornChoiceField

namespace {
class AlicornChoicePopup final : public wxPopupTransientWindow {
public:
   AlicornChoicePopup(
      AlicornChoiceField *owner, const std::vector<wxString> &items,
      int selected, int width)
      : wxPopupTransientWindow(owner, wxBORDER_SIMPLE)
      , mOwner{ owner }
   {
      auto *sizer = safenew wxBoxSizer(wxVERTICAL);
      for (size_t i = 0; i < items.size(); ++i) {
         auto *option = safenew AlicornActionControl(
            this, wxID_ANY, items[i], AUDACITY_ACTION_ICON_NONE,
            wxSize{ std::max(width, 60), kHeight });
         option->SetPressed(static_cast<int>(i) == selected);
         sizer->Add(option, 0, wxEXPAND | wxALL, 1);
         mOptions.push_back(option);
      }
      SetSizerAndFit(sizer);
      // AlicornActionControl sends wxEVT_BUTTON to its parent, so listen here
      // and map the event object back to an item.
      Bind(wxEVT_BUTTON, [this](wxCommandEvent &event) {
         for (size_t i = 0; i < mOptions.size(); ++i) {
            if (event.GetEventObject() == mOptions[i]) {
               mChosen = static_cast<int>(i);
               Dismiss();
               return;
            }
         }
         event.Skip();
      });
      Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent &event) {
         if (event.GetKeyCode() == WXK_ESCAPE)
            Dismiss();
         else
            event.Skip();
      });
   }

   void ShowAt(const wxPoint &screen, int selected)
   {
      Position(screen, GetSize());
      Popup(mOptions.empty()
         ? nullptr
         : mOptions[std::clamp<int>(selected, 0, mOptions.size() - 1)]);
   }

protected:
   void OnDismiss() override
   {
      wxPopupTransientWindow::OnDismiss();
      const auto chosen = mChosen;
      auto *owner = mOwner;
      // Defer so the popup is fully gone before the owner reacts.
      owner->CallAfter([owner, chosen] {
         owner->PopupClosed();
         if (chosen >= 0)
            owner->SelectItem(chosen);
      });
   }

private:
   AlicornChoiceField *mOwner;
   std::vector<AlicornActionControl *> mOptions;
   int mChosen{ -1 };
};
}

AlicornChoiceField::AlicornChoiceField(
   wxWindow *parent, wxWindowID id, const wxString &value,
   const wxPoint &pos, const wxSize &size, const std::vector<wxString> &items)
   : wxControl(parent, id, pos, size, wxBORDER_NONE | wxWANTS_CHARS)
   , mItems{ items }
{
   SetBackgroundStyle(wxBG_STYLE_PAINT);
   SetInitialSize(size);
   for (size_t i = 0; i < mItems.size(); ++i)
      if (mItems[i] == value)
         mSelection = static_cast<int>(i);
   Bind(wxEVT_PAINT, &AlicornChoiceField::OnPaint, this);
   Bind(wxEVT_CHAR_HOOK, &AlicornChoiceField::OnCharHook, this);
   Bind(wxEVT_LEFT_DOWN, &AlicornChoiceField::OnLeftDown, this);
   Bind(wxEVT_SET_FOCUS, &AlicornChoiceField::OnSetFocus, this);
   Bind(wxEVT_KILL_FOCUS, &AlicornChoiceField::OnKillFocus, this);
}

wxSize AlicornChoiceField::DoGetBestSize() const
{
   return wxSize(50, kHeight);
}

wxString AlicornChoiceField::GetValue() const
{
   return mSelection >= 0 ? mItems[mSelection] : wxString{};
}

void AlicornChoiceField::SetValue(const wxString &value)
{
   for (size_t i = 0; i < mItems.size(); ++i) {
      if (mItems[i] == value) {
         mSelection = static_cast<int>(i);
         Refresh(false);
         return;
      }
   }
}

bool AlicornChoiceField::Enable(bool enable)
{
   const bool result = wxControl::Enable(enable);
   Refresh(false);
   return result;
}

void AlicornChoiceField::SelectItem(int index)
{
   if (index < 0 || index >= static_cast<int>(mItems.size()) || index == mSelection)
      return;
   mSelection = index;
   Refresh(false);
   wxCommandEvent event(wxEVT_COMBOBOX, GetId());
   event.SetEventObject(this);
   event.SetString(mItems[index]);
   event.SetInt(index);
   ProcessWindowEvent(event);
}

void AlicornChoiceField::PopupClosed()
{
   mPopupOpen = false;
   Refresh(false);
}

void AlicornChoiceField::OpenPopup()
{
   if (mPopupOpen || !IsEnabled() || mItems.empty())
      return;
   mPopupOpen = true;
   auto *popup = safenew AlicornChoicePopup(
      this, mItems, mSelection, GetClientSize().x);
   popup->ShowAt(ClientToScreen(wxPoint(0, GetClientSize().y)), mSelection);
   Refresh(false);
}

void AlicornChoiceField::OnPaint(wxPaintEvent &)
{
   wxPaintDC dc(this);
   const auto size = GetClientSize();
   if (size.x <= 0 || size.y <= 0)
      return;
   const bool enabled = IsEnabled();
   const bool focused = enabled && (HasFocus() || mPopupOpen);
   const auto colours = Colours(enabled);
   const auto font = GetFont();
   dc.SetFont(font);
   const auto text = GetValue();
   const auto extent = dc.GetTextExtent(text.empty() ? wxString("0") : text);

   AlicornScene scene;
   Frame(scene, wxRect(0, 0, size.x, size.y), colours, focused);
   const int arrowWidth = kStepperWidth;
   ChevronDown(
      scene, wxRect(size.x - arrowWidth, 0, arrowWidth, size.y), colours.text);
   scene.Text(
      wxRect(kTextPad, (size.y - extent.y) / 2,
             std::max(1, size.x - arrowWidth - kTextPad), extent.y),
      text, colours.text, font);
   if (!mScene.Draw(dc, wxRect(0, 0, size.x, size.y), scene)) {
      dc.SetBackground(wxBrush(colours.background));
      dc.Clear();
      dc.SetTextForeground(colours.text);
      dc.DrawText(text, kTextPad, (size.y - extent.y) / 2);
   }
}

void AlicornChoiceField::OnCharHook(wxKeyEvent &event)
{
   if (!HasFocus() || event.HasAnyModifiers()) {
      event.Skip();
      return;
   }
   switch (event.GetKeyCode()) {
   case WXK_SPACE: case WXK_RETURN: case WXK_NUMPAD_ENTER:
   case WXK_DOWN: case WXK_UP:
      OpenPopup();
      return;
   default:
      event.Skip();
   }
}

void AlicornChoiceField::OnLeftDown(wxMouseEvent &)
{
   if (!IsEnabled())
      return;
   SetFocus();
   OpenPopup();
}

void AlicornChoiceField::OnSetFocus(wxFocusEvent &event)
{
   Refresh(false);
   event.Skip();
}

void AlicornChoiceField::OnKillFocus(wxFocusEvent &event)
{
   Refresh(false);
   event.Skip();
}

#endif
