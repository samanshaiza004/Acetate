/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ProjectSnapAdapter.h"

#include "Project.h"
#include "ProjectSnap.h"
#include "ViewInfo.h"

#include <wx/thread.h>

namespace AudacityCaliber {
namespace {
class ProjectDomain final : public SnapDomain {
public:
   explicit ProjectDomain(AudacityProject& project)
      : mProject(project), mProjectId(ProjectSnapId(project)) {}

   void SetEnabled(bool enabled) override
   {
      ProjectSnap::Get(mProject).SetSnapMode(
         enabled ? ::SnapMode::SNAP_NEAREST : ::SnapMode::SNAP_OFF);
   }

   SnapState Read() const override
   {
      const auto& snap = ProjectSnap::Get(mProject);
      const auto& selection = ViewInfo::Get(mProject).selectedRegion;
      SnapMode mode{};
      switch (snap.GetSnapMode()) {
      case ::SnapMode::SNAP_OFF: mode = SnapMode::Off; break;
      case ::SnapMode::SNAP_NEAREST: mode = SnapMode::Nearest; break;
      case ::SnapMode::SNAP_PRIOR: mode = SnapMode::Prior; break;
      default: mode = static_cast<SnapMode>(UINT32_MAX); break;
      }
      // GET is needed to encode the nonlocalized identifier in our protocol.
      const auto snapTo = snap.GetSnapTo();
      const auto identifier = snapTo.GET();
      const auto utf8 = identifier.utf8_str();
      return {mProjectId, mode, selection.t0(), selection.t1(),
         utf8.data() ? std::string{utf8.data(), utf8.length()} : std::string{}};
   }
private:
   AudacityProject& mProject;
   uint64_t mProjectId;
};
}

uint64_t ProjectSnapId(AudacityProject& project) noexcept
{
   return static_cast<uint64_t>(project.GetProjectNumber()) + 1;
}

SnapResult SetProjectSnapEnabled(AudacityProject& project, bool enabled,
   SnapSession& session) noexcept
{
   // This check precedes every Caliber call (and every project access here).
   if (!wxIsMainThread()) {
      SnapResult result;
      result.status = CALIBER_STATUS_INVALID_ARGUMENT;
      return result;
   }
   if (session.ProjectId() != ProjectSnapId(project)) {
      SnapResult result;
      result.status = CALIBER_STATUS_INVALID_ARGUMENT;
      return result;
   }
   ProjectDomain domain{project};
   const auto dispatchStatus = session.Dispatch(enabled);
   if (dispatchStatus != CALIBER_STATUS_OK) {
      SnapResult result;
      result.status = dispatchStatus;
      return result;
   }
   auto applied = session.Pump(domain);
   if (!applied.applyStarted || applied.status != CALIBER_STATUS_OK)
      return applied;
   auto published = session.ReadLatest();
   published.applyStarted = true;
   return published;
}
} // namespace AudacityCaliber
