/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "SnapBridge.h"
#include "OdinSnapBackend.h"

#include <cstddef>
#include <limits>
#include <type_traits>

namespace AudacityCaliber {
namespace {
constexpr size_t MaxSnapToBytes = 1024;

static_assert(sizeof(double) == sizeof(uint64_t), "requires binary64");
static_assert(std::numeric_limits<double>::is_iec559, "requires IEEE 754");
static_assert(std::is_standard_layout_v<AudacityOdinSnapStateV1>);
static_assert(offsetof(CaliberApiV1, context_create) == 8);
static_assert(offsetof(AudacityOdinSnapStateV1, mode) == 8);
static_assert(offsetof(AudacityOdinSnapStateV1, selection_start) == 16);
static_assert(offsetof(AudacityOdinSnapStateV1, snap_to_data) == 32);
static_assert(offsetof(AudacityOdinSnapStateV1, snap_to_len) == 40);

bool SupportsRequiredApi(const CaliberApiV1* api)
{
   return api && api->abi_version == CALIBER_ABI_VERSION_1 &&
      api->struct_size >= offsetof(CaliberApiV1, state_publication_release) +
         sizeof(api->state_publication_release) &&
      api->context_create && api->context_destroy && api->context_dispatch &&
      api->context_take_command && api->context_publish_state &&
      api->context_read_latest_state && api->state_publication_release;
}

struct DomainCallbackContext {
   SnapDomain* domain{};
   SnapState latestState;
};

CaliberStatus SetModeCallback(void* userData, uint32_t mode) noexcept
{
   if (!userData || mode > static_cast<uint32_t>(SnapMode::Prior))
      return CALIBER_STATUS_INVALID_ARGUMENT;
   auto& context = *static_cast<DomainCallbackContext*>(userData);
   try {
      context.domain->SetMode(static_cast<SnapMode>(mode));
      return CALIBER_STATUS_OK;
   }
   catch (...) {
      return CALIBER_STATUS_INTERNAL;
   }
}

CaliberStatus ReadStateCallback(
   void* userData, AudacityOdinSnapStateV1* outState) noexcept
{
   if (!userData || !outState)
      return CALIBER_STATUS_INVALID_ARGUMENT;
   auto& context = *static_cast<DomainCallbackContext*>(userData);
   try {
      context.latestState = context.domain->Read();
      const auto& state = context.latestState;
      outState->project_id = state.projectId;
      outState->mode = static_cast<uint32_t>(state.mode);
      outState->reserved = 0;
      outState->selection_start = state.selectionStart;
      outState->selection_end = state.selectionEnd;
      outState->snap_to_data = reinterpret_cast<const uint8_t*>(state.snapTo.data());
      outState->snap_to_len = state.snapTo.size();
      return CALIBER_STATUS_OK;
   }
   catch (...) {
      return CALIBER_STATUS_INTERNAL;
   }
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
   const auto requiredApiSize =
      offsetof(CaliberApiV1, state_publication_release) +
      sizeof(mApi->state_publication_release);
   if (audacity_odin_snap_backend_api_prefix_size() != requiredApiSize ||
       audacity_odin_snap_backend_state_v1_size() != sizeof(AudacityOdinSnapStateV1) ||
       audacity_odin_snap_backend_session_size() > sizeof(mBackendStorage)) {
      mStatus = CALIBER_STATUS_UNSUPPORTED_VERSION;
      return;
   }
   mStatus = audacity_odin_snap_backend_session_init(
      mBackendStorage.data(), sizeof(mBackendStorage), projectId, mApi);
   mBackendInitialized = mStatus == CALIBER_STATUS_OK;
   if (!mBackendInitialized)
      audacity_odin_snap_backend_session_deinit(mBackendStorage.data());
}

SnapSession::~SnapSession()
{
   if (mBackendInitialized)
      audacity_odin_snap_backend_session_deinit(mBackendStorage.data());
}

bool SnapSession::OnOwnerThread() const noexcept
{
   return std::this_thread::get_id() == mThread;
}

void SnapSession::Invalidate(CaliberStatus status) noexcept
{
   mStatus = status;
   if (mBackendInitialized)
      audacity_odin_snap_backend_invalidate(mBackendStorage.data(), status);
}

CaliberStatus SnapSession::Dispatch(bool enabled) noexcept
{
   if (!OnOwnerThread())
      return CALIBER_STATUS_INVALID_ARGUMENT;
   if (!mBackendInitialized)
      return mStatus;
   return audacity_odin_snap_backend_dispatch(
      mBackendStorage.data(), 1, enabled ? 1 : 0);
}

CaliberStatus SnapSession::DispatchMode(SnapMode mode) noexcept
{
   if (!OnOwnerThread())
      return CALIBER_STATUS_INVALID_ARGUMENT;
   if (!mBackendInitialized)
      return mStatus;
   const auto value = static_cast<uint32_t>(mode);
   if (value > static_cast<uint32_t>(SnapMode::Prior))
      return CALIBER_STATUS_INVALID_ARGUMENT;
   return audacity_odin_snap_backend_dispatch(mBackendStorage.data(), 2, value);
}

SnapResult SnapSession::Pump(SnapDomain& domain) noexcept
{
   SnapResult result;
   if (!OnOwnerThread()) {
      result.status = CALIBER_STATUS_INVALID_ARGUMENT;
      return result;
   }
   if (!mBackendInitialized) {
      result.status = mStatus;
      return result;
   }
   DomainCallbackContext context{&domain};
   uint8_t applyStarted{};
   result.status = audacity_odin_snap_backend_pump(
      mBackendStorage.data(), &context, SetModeCallback, ReadStateCallback,
      &applyStarted);
   result.applyStarted = applyStarted != 0;
   return result;
}

SnapResult SnapSession::ReadLatest() noexcept
{
   SnapResult result;
   if (!OnOwnerThread()) {
      result.status = CALIBER_STATUS_INVALID_ARGUMENT;
      return result;
   }
   if (!mBackendInitialized) {
      result.status = mStatus;
      return result;
   }
   try {
      std::array<uint8_t, MaxSnapToBytes> snapTo{};
      AudacityOdinSnapStateV1 state{};
      size_t required{};
      uint8_t hasState{};
      result.status = audacity_odin_snap_backend_read_latest(
         mBackendStorage.data(), &state, snapTo.data(), snapTo.size(), &required,
         &result.revision, &hasState);
      if (result.status != CALIBER_STATUS_OK)
         return result;
      if (!hasState || state.project_id != mProjectId ||
          state.mode > static_cast<uint32_t>(SnapMode::Prior) ||
          required > snapTo.size()) {
         result.status = CALIBER_STATUS_INVALID_ARGUMENT;
         Invalidate(result.status);
         return result;
      }
      result.state = {
         state.project_id,
         static_cast<SnapMode>(state.mode),
         state.selection_start,
         state.selection_end,
         std::string{reinterpret_cast<const char*>(snapTo.data()), required},
      };
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
