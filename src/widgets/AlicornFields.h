/*  SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#ifdef AUDACITY_USE_ALICORN_SNAP_UI

#include <vector>

#include <wx/control.h>
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

#endif
