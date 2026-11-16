/*  SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <wx/defs.h>

#include "ToolBar.h"
#include "ToolBarButtons.h"

class wxBoxSizer;
class wxCommandEvent;
class wxDC;
class wxWindow;

class FileActionsToolBar final : public ToolBar {
public:
   explicit FileActionsToolBar(AudacityProject& project);
   ~FileActionsToolBar() override = default;

   static Identifier ID();

   void Create(wxWindow* parent) override;
   void Populate() override;
   void Repaint(wxDC* WXUNUSED(dc)) override {}
   void EnableDisableButtons() override;
   void UpdatePrefs() override;
   void OnButton(wxCommandEvent& event);

private:
   void AddActionButton(int id, const TranslatableString& label);
   void RegenerateTooltips() override;

   ToolBarButtons mButtons;
   wxBoxSizer* mToolSizer{};

   DECLARE_CLASS(FileActionsToolBar)
   DECLARE_EVENT_TABLE()
};
