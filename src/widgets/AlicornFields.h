/*  SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#ifdef AUDACITY_USE_ALICORN_SNAP_UI

#include <functional>
#include <vector>

#include <wx/control.h>
#include <wx/arrstr.h>
#include <wx/string.h>

#include "TranslatableString.h"
#include "../alicorn/AlicornTimelineView.h"

// Numeric field with a stepper, painted through a retained Alicorn scene.
// Editing is limited to number entry (digits, one decimal point and a leading
// minus when negative values are allowed), so no native text input or IME
// composition is involved. The control owns its value and sends
// wxEVT_SPINCTRL when the user commits a change, like SpinControl.
class AlicornSpinField final : public wxControl {
public:
   AlicornSpinField(
      wxWindow *parent, wxWindowID id, double value, double min, double max,
      double step, bool allowFractional, const wxPoint &pos,
      const wxSize &size);

   void SetValue(double value);
   double GetValue() const { return mValue; }
   void SetName(const TranslatableString &name);
   using wxControl::SetName;

   bool AcceptsFocus() const override { return IsEnabled(); }
   bool Enable(bool enable = true) override;

protected:
   wxSize DoGetBestSize() const override;

private:
   wxString FormatValue() const;
   void SetValueInternal(double value, bool notify);
   void Commit();
   void Step(double direction);
   void InsertText(const wxString &text);
   int CaretFromX(int x) const;
   wxRect StepperRect() const;

   void OnPaint(wxPaintEvent &event);
   void OnCharHook(wxKeyEvent &event);
   void OnLeftDown(wxMouseEvent &event);
   void OnWheel(wxMouseEvent &event);
   void OnSetFocus(wxFocusEvent &event);
   void OnKillFocus(wxFocusEvent &event);

   AlicornSceneView mScene;
   double mValue{};
   double mMin{};
   double mMax{};
   double mStep{ 1.0 };
   bool mFractional{};
   bool mEditing{};
   bool mSelectAll{};
   wxString mBuffer;
   int mCaret{};
};

// Read-only drop-down list. The list is a transient popup of Alicorn action
// controls; selecting an item sends wxEVT_COMBOBOX.
class AlicornChoiceField final : public wxControl {
public:
   AlicornChoiceField(
      wxWindow *parent, wxWindowID id, const wxString &value,
      const wxPoint &pos, const wxSize &size,
      const std::vector<wxString> &items);

   wxString GetValue() const;
   void SetValue(const wxString &value);

   // The subset of wxChoice that the audio device toolbar relies on.
   int GetSelection() const { return mSelection; }
   void SetSelection(int index);
   wxString GetString(unsigned index) const;
   wxString GetStringSelection() const { return GetValue(); }
   bool SetStringSelection(const wxString &value);
   int FindString(const wxString &value) const;
   unsigned GetCount() const { return static_cast<unsigned>(mItems.size()); }
   wxArrayString GetStrings() const;
   void Clear();
   void Append(const wxString &item);
   void Append(const wxArrayString &items);

   bool AcceptsFocus() const override { return IsEnabled(); }
   bool Enable(bool enable = true) override;

   // Called by the popup.
   void SelectItem(int index);
   void PopupClosed();

protected:
   wxSize DoGetBestSize() const override;

private:
   void OpenPopup();

   void OnPaint(wxPaintEvent &event);
   void OnCharHook(wxKeyEvent &event);
   void OnLeftDown(wxMouseEvent &event);
   void OnSetFocus(wxFocusEvent &event);
   void OnKillFocus(wxFocusEvent &event);

   AlicornSceneView mScene;
   std::vector<wxString> mItems;
   int mSelection{ -1 };
   bool mPopupOpen{};
};

// Drop-down that presents a grouped list of choices (headings and nested
// items flattened into columns), used where the wx build uses a popup menu.
// The owner supplies the entries and learns about selections through a
// handler; the control keeps only the display text and the current key.
class AlicornMenuField final : public wxControl {
public:
   struct Entry {
      wxString key;      // identifies the choice; empty for a heading
      wxString label;
      int depth{};       // heading nesting, for indentation
      bool heading{};
   };

   AlicornMenuField(
      wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size);

   void SetEntries(std::vector<Entry> entries);
   void SetCurrentKey(const wxString &key);
   wxString GetCurrentKey() const { return mCurrentKey; }
   void SetSelectionHandler(std::function<void(const wxString &key)> handler)
   {
      mHandler = std::move(handler);
   }
   void SelectNone() {}

   bool AcceptsFocus() const override { return IsEnabled(); }
   bool Enable(bool enable = true) override;

   // Called by the popup.
   void Choose(const wxString &key);
   void PopupClosed();

protected:
   wxSize DoGetBestSize() const override;

private:
   void OpenPopup();
   void Step(int direction);
   wxString CurrentLabel() const;

   void OnPaint(wxPaintEvent &event);
   void OnCharHook(wxKeyEvent &event);
   void OnLeftDown(wxMouseEvent &event);
   void OnSetFocus(wxFocusEvent &event);
   void OnKillFocus(wxFocusEvent &event);

   AlicornSceneView mScene;
   std::vector<Entry> mEntries;
   wxString mCurrentKey;
   std::function<void(const wxString &key)> mHandler;
   bool mPopupOpen{};
};

#endif
