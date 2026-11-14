/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <catch2/catch.hpp>

#include "ProjectSnapAdapter.h"
#include "Project.h"
#include "ProjectSnap.h"
#include "ViewInfo.h"
#include "MockedAudio.h"
#include "MockedPrefs.h"

#include "AudioIOBase.h"
#include <portaudio.h>
#include <wx/init.h>
#include <thread>

TEST_CASE("Caliber Snap adapter uses real project state and notifications", "[caliber]")
{
   wxInitializer wx;
   REQUIRE(wx.IsOk());
   MockedPrefs prefs;
   MockedAudio audio;
   if (Pa_GetDefaultOutputDevice() == paNoDevice ||
       Pa_GetDefaultInputDevice() == paNoDevice)
      REQUIRE(AudioIOBase::GetOptimalSupportedSampleRate() == 44100);
   auto first = AudacityProject::Create();
   auto second = AudacityProject::Create();
   auto& firstSnap = ProjectSnap::Get(*first);
   auto& secondSnap = ProjectSnap::Get(*second);
   firstSnap.SetSnapMode(SnapMode::SNAP_OFF);
   secondSnap.SetSnapMode(SnapMode::SNAP_OFF);
   AudacityCaliber::SnapSession firstSession(AudacityCaliber::ProjectSnapId(*first));
   AudacityCaliber::SnapSession secondSession(AudacityCaliber::ProjectSnapId(*second));
   firstSnap.SetSnapTo("seconds");
   secondSnap.SetSnapTo("seconds");
   auto& selection = ViewInfo::Get(*first).selectedRegion;
   auto& otherSelection = ViewInfo::Get(*second).selectedRegion;
   selection.setTimes(1.2, 2.8);
   otherSelection.setTimes(4.2, 5.8);
   // Model the application's synchronous selection observer without
   // initializing ProjectRate, whose default deliberately queries audio
   // devices (unavailable on some headless test hosts).
   auto snapSelection = [](AudacityProject& project) {
      auto& snap = ProjectSnap::Get(project);
      auto* projectPtr = &project;
      auto* snapPtr = &snap;
      return snap.Subscribe([projectPtr, snapPtr](const SnapChangedMessage&) {
         if (snapPtr->GetSnapMode() == SnapMode::SNAP_OFF)
            return;
         auto& region = ViewInfo::Get(*projectPtr).selectedRegion;
         const auto start = snapPtr->SnapTime(region.t0()).time;
         const auto end = snapPtr->SnapTime(region.t1()).time;
         region.setTimes(start, end);
      });
   };
   auto firstSelectionObserver = snapSelection(*first);
   auto secondSelectionObserver = snapSelection(*second);
   int notifications = 0;
   auto subscription = firstSnap.Subscribe([&](const SnapChangedMessage&) { ++notifications; });

   auto result = AudacityCaliber::SetProjectSnapEnabled(*first, true, firstSession);
   REQUIRE(result.status == CALIBER_STATUS_OK);
   REQUIRE(result.revision == 1);
   REQUIRE(result.applyStarted);
   REQUIRE(result.hasState);
   REQUIRE(firstSnap.GetSnapMode() == SnapMode::SNAP_NEAREST);
   REQUIRE(result.state.mode == AudacityCaliber::SnapMode::Nearest);
   REQUIRE(result.state.snapTo == "seconds");
   REQUIRE(result.state.selectionStart == Approx(1.0));
   REQUIRE(result.state.selectionEnd == Approx(3.0));
   REQUIRE(selection.t0() == Approx(result.state.selectionStart));
   REQUIRE(selection.t1() == Approx(result.state.selectionEnd));
   REQUIRE(notifications == 1);
   REQUIRE(secondSnap.GetSnapMode() == SnapMode::SNAP_OFF);
   REQUIRE(otherSelection.t0() == Approx(4.2));
   REQUIRE(otherSelection.t1() == Approx(5.8));

   // A session is bound to one project; using the other project's session
   // cannot mutate the model or consume its revision sequence.
   auto mismatched = AudacityCaliber::SetProjectSnapEnabled(*first, false, secondSession);
   REQUIRE(mismatched.status == CALIBER_STATUS_INVALID_ARGUMENT);
   REQUIRE_FALSE(mismatched.applyStarted);
   REQUIRE(firstSnap.GetSnapMode() == SnapMode::SNAP_NEAREST);
   REQUIRE(secondSession.ReadLatest().revision == 0);

   const auto firstId = result.state.projectId;
   result = AudacityCaliber::SetProjectSnapEnabled(*second, true, secondSession);
   REQUIRE(result.hasState);
   REQUIRE(result.state.projectId != firstId);
   REQUIRE(result.state.selectionStart == Approx(4.0));
   REQUIRE(result.state.selectionEnd == Approx(6.0));

   result = AudacityCaliber::SetProjectSnapEnabled(*first, false, firstSession);
   REQUIRE(result.hasState);
   REQUIRE(result.revision == 2);
   REQUIRE(result.state.mode == AudacityCaliber::SnapMode::Off);
   REQUIRE(firstSnap.GetSnapMode() == SnapMode::SNAP_OFF);
   REQUIRE(notifications == 2);

   // An ordinary existing caller still publishes; a checkbox enable maps prior
   // to nearest exactly like the original handler.
   firstSnap.SetSnapMode(SnapMode::SNAP_PRIOR);
   REQUIRE(notifications == 3);
   result = AudacityCaliber::SetProjectSnapEnabled(*first, true, firstSession);
   REQUIRE(result.hasState);
   REQUIRE(result.revision == 3);
   REQUIRE(result.state.mode == AudacityCaliber::SnapMode::Nearest);
   REQUIRE(firstSnap.GetSnapMode() == SnapMode::SNAP_NEAREST);
   REQUIRE(notifications == 4);

   // A later observer may change the mode again while handling the original
   // notification. The published state must describe the final project state.
   firstSnap.SetSnapMode(SnapMode::SNAP_OFF);
   selection.setTimes(1.8, 2.8);
   auto overrideMode = firstSnap.Subscribe([&](const SnapChangedMessage& message) {
      if (message.newSnapMode == SnapMode::SNAP_NEAREST)
         firstSnap.SetSnapMode(SnapMode::SNAP_PRIOR);
   });
   result = AudacityCaliber::SetProjectSnapEnabled(*first, true, firstSession);
   REQUIRE(result.hasState);
   REQUIRE(result.state.mode == AudacityCaliber::SnapMode::Prior);
   REQUIRE(firstSnap.GetSnapMode() == SnapMode::SNAP_PRIOR);
   REQUIRE(result.state.selectionStart == Approx(selection.t0()));
   REQUIRE(result.state.selectionEnd == Approx(selection.t1()));

   // The production entry point rejects non-UI threads before touching Caliber.
   AudacityCaliber::SnapResult offThread;
   std::thread worker([&] {
      offThread = AudacityCaliber::SetProjectSnapEnabled(*first, false, firstSession);
   });
   worker.join();
   REQUIRE(offThread.status == CALIBER_STATUS_INVALID_ARGUMENT);
   REQUIRE_FALSE(offThread.applyStarted);
   REQUIRE(firstSnap.GetSnapMode() == SnapMode::SNAP_PRIOR);
}
