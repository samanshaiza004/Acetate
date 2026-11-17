/**********************************************************************

  Audacity: A Digital Audio Editor

  DeviceToolbar.h

  Dominic Mazzoni

**********************************************************************/

#ifndef __AUDACITY_DEVICE_TOOLBAR__
#define __AUDACITY_DEVICE_TOOLBAR__

#include <vector>
#include "ToolBar.h"
#include "Observer.h"

enum class DeviceChangeMessage : char;

class wxSize;
class wxPoint;
#ifdef AUDACITY_USE_ALICORN_SNAP_UI
class AlicornChoiceField;
using DeviceChoice = AlicornChoiceField;
#else
class wxChoice;
using DeviceChoice = wxChoice;
#endif
struct DeviceSourceMap;

class AudacityProject;

class DeviceToolBar final : public ToolBar {

 public:

   static Identifier ID();

   DeviceToolBar( AudacityProject &project );
   virtual ~DeviceToolBar();

   bool ShownByDefault() const override;

   static DeviceToolBar &Get( AudacityProject &project );
   static const DeviceToolBar &Get( const AudacityProject &project );

   void Create(wxWindow * parent) override;

   void UpdatePrefs() override;
   void UpdateSelectedPrefs( int ) override;

   void DeinitChildren();
   void Populate() override;
   void Repaint(wxDC * WXUNUSED(dc)) override {};
   void EnableDisableButtons() override;
   void OnFocus(wxFocusEvent &event);
   void OnCaptureKey(wxCommandEvent &event);

   void OnChoice(wxCommandEvent & event);

   /// When the prefs don't exist this value is used.
   /// 883 takes a complete row in the default initial size of Audacity.
   int GetInitialWidth()  override{ return 883; }
   int GetMinToolbarWidth() override { return 350; }

   void ShowInputDialog();
   void ShowOutputDialog();
   void ShowHostDialog();
   void ShowChannelsDialog();

 private:
   void OnRescannedDevices(DeviceChangeMessage);

   int  ChangeHost();
   void ChangeDevice(bool isInput);
   void RefillCombos();
   void FillHosts();
   void FillHostDevices();
   void FillInputChannels();
   void SetDevices(const DeviceSourceMap *in, const DeviceSourceMap *out);
   void SetNames();
   void RegenerateTooltips() override;
   void ShowComboDialog(DeviceChoice *combo, const TranslatableString &title);

   DeviceChoice *mInput;
   DeviceChoice *mOutput;
   DeviceChoice *mInputChannels;
   DeviceChoice *mHost;

   Observer::Subscription mSubscription;

 public:

   DECLARE_CLASS(DeviceToolBar)
   DECLARE_EVENT_TABLE()
};

int DeviceToolbarPrefsID();

#endif

