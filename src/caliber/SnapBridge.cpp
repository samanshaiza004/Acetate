/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "SnapBridge.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <cmath>

namespace AudacityCaliber {
namespace {
constexpr size_t CommandSize = 24;
constexpr size_t StateHeaderSize = 40;
constexpr size_t MaxSnapToBytes = 1024;
constexpr uint32_t StateSchema = 0x41535301; // application: AS state v1

void Put(uint8_t* bytes, size_t at, uint64_t value, size_t count)
{
   for (size_t i = 0; i < count; ++i)
      bytes[at + i] = static_cast<uint8_t>(value >> (8 * i));
}
uint64_t Get(const uint8_t* bytes, size_t at, size_t count)
{
   uint64_t value{};
   for (size_t i = 0; i < count; ++i)
      value |= uint64_t{bytes[at + i]} << (8 * i);
   return value;
}
void PutDouble(uint8_t* bytes, size_t at, double value)
{
   static_assert(sizeof(double) == sizeof(uint64_t), "requires binary64");
   static_assert(std::numeric_limits<double>::is_iec559, "requires IEEE 754");
   uint64_t bits;
   std::memcpy(&bits, &value, sizeof(bits));
   Put(bytes, at, bits, sizeof(bits));
}
double GetDouble(const uint8_t* bytes, size_t at)
{
   const auto bits = Get(bytes, at, 8);
   double value;
   std::memcpy(&value, &bits, sizeof(value));
   return value;
}
bool HasMagic(const uint8_t* bytes, const char* magic)
{
   return std::memcmp(bytes, magic, 4) == 0;
}
struct PublicationOwner {
   const CaliberApiV1* api;
   CaliberStatePublication publication{};
   bool acquired{};
   ~PublicationOwner() { if (acquired) api->state_publication_release(&publication); }
};
struct PumpGuard {
   bool& active;
   ~PumpGuard() { active = false; }
};

bool SupportsRequiredApi(const CaliberApiV1* api)
{
   // Do not inspect a trailing pointer until its complete field is present.
   return api && api->abi_version == CALIBER_ABI_VERSION_1 &&
      api->struct_size >= offsetof(CaliberApiV1, state_publication_release) +
         sizeof(api->state_publication_release) &&
      api->context_create && api->context_destroy && api->context_dispatch &&
      api->context_take_command && api->context_publish_state &&
      api->context_read_latest_state && api->state_publication_release;
}
bool ReadState(const CaliberStatePublication& pub, uint64_t projectId,
   uint64_t revision, SnapState& state)
{
   if (pub.schema != StateSchema || pub.reserved != 0 || pub.revision != revision ||
       pub.len < StateHeaderSize || !pub.data || !HasMagic(pub.data, "ASST") ||
       Get(pub.data, 4, 2) != 1 || Get(pub.data, 6, 2) != 0 ||
       Get(pub.data, 8, 8) != projectId || Get(pub.data, 16, 4) > 2)
      return false;
   const auto length = Get(pub.data, 20, 4);
   if (length > MaxSnapToBytes || length != pub.len - StateHeaderSize)
      return false;
   const auto selectionStart = GetDouble(pub.data, 24);
   const auto selectionEnd = GetDouble(pub.data, 32);
   if (!std::isfinite(selectionStart) || !std::isfinite(selectionEnd) ||
       selectionStart > selectionEnd)
      return false;
   state.projectId = projectId;
   state.mode = static_cast<SnapMode>(Get(pub.data, 16, 4));
   state.selectionStart = selectionStart;
   state.selectionEnd = selectionEnd;
   state.snapTo.assign(reinterpret_cast<const char*>(pub.data + StateHeaderSize),
      static_cast<size_t>(length));
   return true;
}
} // namespace

SnapSession::SnapSession(uint64_t projectId, const CaliberApiV1* api) noexcept
   : mProjectId(projectId), mThread(std::this_thread::get_id())
{
   if (!projectId) {
      mStatus = CALIBER_STATUS_INVALID_ARGUMENT;
      return;
   }
   mApi = api ? api : caliber_get_api(CALIBER_ABI_VERSION_1);
   if (!SupportsRequiredApi(mApi)) {
      mApi = nullptr;
      mStatus = CALIBER_STATUS_UNSUPPORTED_VERSION;
      return;
   }
   CaliberContextConfig config{};
   config.struct_size = sizeof(config);
   config.max_command_bytes = CommandSize;
   config.max_publication_bytes = StateHeaderSize + MaxSnapToBytes;
   config.max_pending_commands = 2;
   mStatus = mApi->context_create(&config, &mContext);
   if (mStatus == CALIBER_STATUS_OK && !mContext)
      mStatus = CALIBER_STATUS_INTERNAL;
   if (mStatus != CALIBER_STATUS_OK && mContext) {
      mApi->context_destroy(mContext);
      mContext = nullptr;
   }
}
SnapSession::~SnapSession()
{
   if (mContext)
      mApi->context_destroy(mContext);
}
bool SnapSession::OnOwnerThread() const noexcept
{
   return std::this_thread::get_id() == mThread;
}
void SnapSession::Invalidate(CaliberStatus status) noexcept
{
   mStatus = status;
   mPending = false;
   if (mContext) {
      mApi->context_destroy(mContext);
      mContext = nullptr;
   }
}

CaliberStatus SnapSession::Dispatch(bool enabled) noexcept
{
   if (!OnOwnerThread())
      return CALIBER_STATUS_INVALID_ARGUMENT;
   if (!mContext)
      return mStatus;
   if (mPumping)
      return CALIBER_STATUS_UNAVAILABLE; // reentrant UI action; do not kill outer pump
   if (mPending) {
      // Never leave an older accepted command around after a caller falls back.
      Invalidate(CALIBER_STATUS_QUEUE_FULL);
      return mStatus;
   }
   mCommand.fill(0);
   std::memcpy(mCommand.data(), "ASCM", 4);
   Put(mCommand.data(), 4, 1, 2);
   Put(mCommand.data(), 6, 1, 2);
   Put(mCommand.data(), 8, mProjectId, 8);
   Put(mCommand.data(), 16, enabled ? 1 : 0, 4);
   const auto status = mApi->context_dispatch(mContext, mCommand.data(), mCommand.size());
   if (status != CALIBER_STATUS_OK) {
      Invalidate(status); // discard any ambiguous work before direct fallback
      return status;
   }
   mPending = true;
   return CALIBER_STATUS_OK;
}

SnapResult SnapSession::Pump(SnapDomain& domain) noexcept
{
   SnapResult result;
   if (!OnOwnerThread()) {
      result.status = CALIBER_STATUS_INVALID_ARGUMENT;
      return result;
   }
   if (!mContext) {
      result.status = mStatus;
      return result;
   }
   if (mPumping || !mPending) {
      result.status = CALIBER_STATUS_UNAVAILABLE;
      return result;
   }
   mPumping = true;
   PumpGuard guard{mPumping};
   try {
      std::array<uint8_t, CommandSize> received{};
      size_t receivedSize{};
      result.status = mApi->context_take_command(mContext, received.data(), received.size(), &receivedSize);
      if (result.status != CALIBER_STATUS_OK) {
         Invalidate(result.status);
         return result;
      }
      mPending = false;
      if (receivedSize != CommandSize ||
          !std::equal(received.begin(), received.end(), mCommand.begin()) ||
          !HasMagic(received.data(), "ASCM") || Get(received.data(), 4, 2) != 1 ||
          Get(received.data(), 6, 2) != 1 || Get(received.data(), 8, 8) != mProjectId ||
          Get(received.data(), 16, 4) > 1 || Get(received.data(), 20, 4) != 0) {
         result.status = CALIBER_STATUS_INVALID_ARGUMENT;
         Invalidate(result.status);
         return result;
      }
      result.applyStarted = true;
      domain.SetEnabled(Get(received.data(), 16, 4) != 0);
      // Audacity's synchronous observers may change selection before return.
      const auto state = domain.Read();
      if (state.projectId != mProjectId || static_cast<uint32_t>(state.mode) > 2) {
         result.status = CALIBER_STATUS_INVALID_ARGUMENT;
         Invalidate(result.status);
         return result;
      }
      if (state.snapTo.size() > MaxSnapToBytes) {
         result.status = CALIBER_STATUS_LIMIT_EXCEEDED;
         Invalidate(result.status);
         return result;
      }
      std::vector<uint8_t> bytes(StateHeaderSize + state.snapTo.size());
      std::memcpy(bytes.data(), "ASST", 4);
      Put(bytes.data(), 4, 1, 2);
      Put(bytes.data(), 8, mProjectId, 8);
      Put(bytes.data(), 16, static_cast<uint32_t>(state.mode), 4);
      Put(bytes.data(), 20, state.snapTo.size(), 4);
      PutDouble(bytes.data(), 24, state.selectionStart);
      PutDouble(bytes.data(), 32, state.selectionEnd);
      std::memcpy(bytes.data() + StateHeaderSize, state.snapTo.data(), state.snapTo.size());
      result.status = mApi->context_publish_state(mContext, StateSchema, bytes.data(), bytes.size(), &result.revision);
      if (result.status != CALIBER_STATUS_OK || result.revision <= mRevision) {
         if (result.status == CALIBER_STATUS_OK)
            result.status = CALIBER_STATUS_INTERNAL;
         Invalidate(result.status);
         return result;
      }
      mRevision = result.revision;
      mPublishedBytes = std::move(bytes);
      return result;
   }
   catch (...) {
      result.status = CALIBER_STATUS_INTERNAL;
      Invalidate(result.status);
      return result;
   }
}

SnapResult SnapSession::ReadLatest() noexcept
{
   SnapResult result;
   if (!OnOwnerThread()) {
      result.status = CALIBER_STATUS_INVALID_ARGUMENT;
      return result;
   }
   if (!mContext) {
      result.status = mStatus;
      return result;
   }
   if (mPumping || !mRevision) {
      result.status = CALIBER_STATUS_UNAVAILABLE;
      return result;
   }
   try {
      PublicationOwner view{mApi};
      result.status = mApi->context_read_latest_state(mContext, &view.publication);
      if (result.status != CALIBER_STATUS_OK) {
         Invalidate(result.status);
         return result;
      }
      view.acquired = true;
      if (view.publication.len != mPublishedBytes.size() || !view.publication.data ||
          std::memcmp(view.publication.data, mPublishedBytes.data(), mPublishedBytes.size()) != 0 ||
          !ReadState(view.publication, mProjectId, mRevision, result.state)) {
         result.status = CALIBER_STATUS_INVALID_ARGUMENT;
         Invalidate(result.status);
         return result;
      }
      result.revision = view.publication.revision;
      result.hasState = true;
      return result;
   }
   catch (...) {
      result.status = CALIBER_STATUS_INTERNAL;
      Invalidate(result.status);
      return result;
   }
}
} // namespace AudacityCaliber
