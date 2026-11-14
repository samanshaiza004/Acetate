/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <caliber.h>

#include <array>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace AudacityCaliber {

// Values are part of the application-owned v1 protocol, not Caliber enums.
enum class SnapMode : uint32_t { Off = 0, Nearest = 1, Prior = 2 };

struct SnapState {
   uint64_t projectId{};
   SnapMode mode{SnapMode::Off};
   double selectionStart{};
   double selectionEnd{};
   std::string snapTo;
};

// Caliber never stores or calls the application adapter. The pump calls it
// synchronously on the UI thread.
class SnapDomain {
public:
   virtual ~SnapDomain() = default;
   virtual void SetEnabled(bool enabled) = 0;
   virtual SnapState Read() const = 0;
};

struct SnapResult {
   CaliberStatus status{CALIBER_STATUS_INTERNAL};
   // True once the domain operation has started, even if it throws. Never
   // replay such an operation when recovering from a boundary failure.
   bool applyStarted{};
   bool hasState{};
   uint64_t revision{};
   SnapState state;
};

// One context per toolbar/project session. Construct, use and destroy on the
// UI thread; generic Caliber operations are never safe for audio callbacks.
// Dispatch and ReadLatest are frontend operations; Pump is the current C++
// application backend. No callbacks or threads are retained by Caliber.
class SnapSession final {
public:
   explicit SnapSession(uint64_t projectId, const CaliberApiV1* api = nullptr) noexcept;
   ~SnapSession();
   SnapSession(const SnapSession&) = delete;
   SnapSession& operator=(const SnapSession&) = delete;

   uint64_t ProjectId() const noexcept { return mProjectId; }
   CaliberStatus Dispatch(bool enabled) noexcept;
   SnapResult Pump(SnapDomain& domain) noexcept;
   SnapResult ReadLatest() noexcept;

private:
   bool OnOwnerThread() const noexcept;
   void Invalidate(CaliberStatus status) noexcept;

   const uint64_t mProjectId;
   const std::thread::id mThread;
   const CaliberApiV1* mApi{};
   CaliberContext* mContext{};
   CaliberStatus mStatus{CALIBER_STATUS_INTERNAL};
   bool mPending{};
   bool mPumping{};
   uint64_t mRevision{};
   std::array<uint8_t, 24> mCommand{};
   std::vector<uint8_t> mPublishedBytes;
};

} // namespace AudacityCaliber
