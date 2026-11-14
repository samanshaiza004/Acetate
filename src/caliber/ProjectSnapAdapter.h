/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "SnapBridge.h"

class AudacityProject;

namespace AudacityCaliber {
uint64_t ProjectSnapId(AudacityProject& project) noexcept;
// UI thread only. The project must remain alive for this synchronous call.
SnapResult SetProjectSnapEnabled(AudacityProject& project, bool enabled,
   SnapSession& session) noexcept;
}
