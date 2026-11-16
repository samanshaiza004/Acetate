/*  SPDX-License-Identifier: GPL-2.0-or-later */

#include "FileActionsToolBar.h"

#include <wx/button.h>
#include <wx/sizer.h>

#include "AllThemeResources.h"
#include "Project.h"
#ifdef AUDACITY_USE_ALICORN_SNAP_UI
#include "AlicornActionControl.h"
#endif
#include "CommandContext.h"
#include "ToolManager.h"

namespace {
enum {
   ImportAudioID,
   SaveProjectID,
   ExportAudioID,
   ActionCount,
};

constexpr int FirstActionID = 22500;

const ToolBarButtons::ButtonList FileActionButtons{
   { ImportAudioID, wxT("ImportAudio"), XO("Import Audio") },
   { SaveProjectID, wxT("Save"), XO("Save Project") },
   { ExportAudioID, wxT("Export"), XO("Export Audio") },
};
}

IMPLEMENT_CLASS(FileActionsToolBar, ToolBar);

BEGIN_EVENT_TABLE(FileActionsToolBar, ToolBar)
   EVT_COMMAND_RANGE(FirstActionID,
      FirstActionID + ActionCount - 1,
      wxEVT_BUTTON,
      FileActionsToolBar::OnButton)
END_EVENT_TABLE()

FileActionsToolBar::FileActionsToolBar(AudacityProject& project)
   : ToolBar(project, XO("File Actions"), ID())
   , mButtons{ this, project, FileActionButtons, ActionCount, FirstActionID }
{}

Identifier FileActionsToolBar::ID()
{
   return wxT("FileActions");
}

void FileActionsToolBar::Create(wxWindow* parent)
{
   ToolBar::Create(parent);
   UpdatePrefs();
}

void FileActionsToolBar::AddActionButton(
   int id, const TranslatableString& label)
{
   wxWindow* button{};
#ifdef AUDACITY_USE_ALICORN_SNAP_UI
   auto alicornButton = safenew AlicornActionControl(
      this,
      FirstActionID + id,
      label.Translation(),
      AUDACITY_ACTION_ICON_NONE,
      wxSize{ 112, 40 });
   alicornButton->SetName(label.Translation());
   button = alicornButton;
#else
   auto nativeButton = safenew wxButton(
      this, FirstActionID + id, label.Translation());
   nativeButton->SetMinSize(wxSize{ 112, 40 });
   button = nativeButton;
#endif
   mButtons.RegisterButton(id, button);
   mToolSizer->Add(button, 0, wxALIGN_CENTER_VERTICAL | wxALL, toolbarSpacing);
}

void FileActionsToolBar::Populate()
{
   SetBackgroundColour(theTheme.Colour(clrMedium));
   Add(mToolSizer = safenew wxBoxSizer(wxHORIZONTAL),
      0, wxALIGN_CENTRE | wxALL, toolbarSpacing);

   AddActionButton(ImportAudioID, XO("Import Audio"));
   AddActionButton(SaveProjectID, XO("Save Project"));
   AddActionButton(ExportAudioID, XO("Export Audio"));

   EnableDisableButtons();
   RegenerateTooltips();
}

void FileActionsToolBar::EnableDisableButtons()
{
   mButtons.EnableDisableButtons();
}

void FileActionsToolBar::UpdatePrefs()
{
   RegenerateTooltips();
   SetLabel(XO("File Actions"));
   ToolBar::UpdatePrefs();
}

void FileActionsToolBar::RegenerateTooltips()
{
   mButtons.RegenerateTooltips();
}

void FileActionsToolBar::OnButton(wxCommandEvent& event)
{
   mButtons.OnButton(event);
}

static RegisteredToolbarFactory factory{
   [](AudacityProject& project) {
      return ToolBar::Holder{ safenew FileActionsToolBar{ project } };
   }
};

namespace {
AttachedToolBarMenuItem sAttachment{
   FileActionsToolBar::ID(),
   wxT("ShowFileActionsTB"),
   XXO("&File Actions Toolbar"),
   { Registry::OrderingHint::Before, "ShowEditTB" }
};
}
