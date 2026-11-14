/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "SnapBridge.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace AudacityCaliber;
namespace {
void Check(bool value, const char* expression, int line)
{
   if (!value)
      throw std::runtime_error(std::string{"line "} + std::to_string(line) + ": " + expression);
}
#define CHECK(x) Check((x), #x, __LINE__)

struct Domain final : SnapDomain {
   SnapState state;
   int calls{};
   bool setPriorOnEnable{};
   bool throws{};
   explicit Domain(uint64_t id) : state{id, SnapMode::Off, 1.2, 2.8, "seconds"} {}
   void SetEnabled(bool enabled) override {
      ++calls;
      state.mode = enabled ? SnapMode::Nearest : SnapMode::Off;
      if (enabled) {
         state.selectionStart = std::round(state.selectionStart);
         state.selectionEnd = std::round(state.selectionEnd);
         if (setPriorOnEnable) state.mode = SnapMode::Prior;
      }
      if (throws) throw std::runtime_error("domain failure after mutation");
   }
   SnapState Read() const override { return state; }
};

const CaliberApiV1* realApi{};
int releases{};
int destroys{};
std::vector<uint8_t> sent;

CaliberStatus ObserveDispatch(const CaliberContext* context, const uint8_t* data, size_t length) {
   sent.assign(data, data + length);
   return realApi->context_dispatch(context, data, length);
}
CaliberStatus FillAndDispatch(const CaliberContext* context, const uint8_t* data, size_t length) {
   // Fill the actual Caliber queue; the bridge's own dispatch must see FULL.
   CHECK(realApi->context_dispatch(context, data, length) == CALIBER_STATUS_OK);
   CHECK(realApi->context_dispatch(context, data, length) == CALIBER_STATUS_OK);
   return realApi->context_dispatch(context, data, length);
}
CaliberStatus FailPublish(const CaliberContext*, uint32_t, const uint8_t*, size_t, uint64_t*) {
   return CALIBER_STATUS_INTERNAL;
}
CaliberStatus FailRead(const CaliberContext*, CaliberStatePublication*) {
   return CALIBER_STATUS_UNAVAILABLE;
}
CaliberStatus CorruptRead(const CaliberContext* context, CaliberStatePublication* out) {
   auto status = realApi->context_read_latest_state(context, out);
   if (status == CALIBER_STATUS_OK) ++out->schema;
   return status;
}
CaliberStatus CorruptCommand(const CaliberContext* context, uint8_t* out, size_t cap, size_t* len) {
   auto status = realApi->context_take_command(context, out, cap, len);
   if (status == CALIBER_STATUS_OK) out[4] = 2;
   return status;
}
CaliberStatus FailTake(const CaliberContext*, uint8_t*, size_t, size_t*) {
   return CALIBER_STATUS_INTERNAL;
}
void CountRelease(CaliberStatePublication* state) {
   ++releases;
   realApi->state_publication_release(state);
}
void CountDestroy(CaliberContext* context) {
   ++destroys;
   realApi->context_destroy(context);
}

SnapResult SetSnapEnabled(SnapSession& session, bool enabled, Domain& domain) {
   if (session.ProjectId() != domain.state.projectId) {
      SnapResult result;
      result.status = CALIBER_STATUS_INVALID_ARGUMENT;
      return result;
   }
   const auto status = session.Dispatch(enabled);
   if (status != CALIBER_STATUS_OK) {
      SnapResult result;
      result.status = status;
      return result;
   }
   auto result = session.Pump(domain);
   if (result.status != CALIBER_STATUS_OK || !result.applyStarted)
      return result;
   auto publication = session.ReadLatest();
   publication.applyStarted = true;
   return publication;
}

void Run()
{
   realApi = caliber_get_api(CALIBER_ABI_VERSION_1);
   CHECK(realApi != nullptr);
   auto api = *realApi;
   api.context_dispatch = ObserveDispatch;
   api.state_publication_release = CountRelease;
   api.context_destroy = CountDestroy;
   Domain first{101}, second{202};
   SnapSession firstSession(101, &api), secondSession(202, &api);
   auto result = SetSnapEnabled(firstSession, true, first);
   CHECK(result.status == CALIBER_STATUS_OK && result.applyStarted && result.hasState);
   CHECK(result.revision == 1 && result.state.projectId == 101);
   CHECK(result.state.mode == SnapMode::Nearest && result.state.snapTo == "seconds");
   CHECK(result.state.selectionStart == 1.0 && result.state.selectionEnd == 3.0);
   CHECK(first.calls == 1 && second.calls == 0);
   CHECK(sent.size() == 24 && std::memcmp(sent.data(), "ASCM", 4) == 0);
   CHECK(sent[4] == 1 && sent[6] == 1 && sent[8] == 101 && sent[16] == 1);
   CHECK(releases == 1 && destroys == 0);
   result = SetSnapEnabled(secondSession, true, second);
   CHECK(result.hasState && result.state.projectId == 202 && second.calls == 1);
   result = SetSnapEnabled(firstSession, false, first);
   CHECK(result.hasState && result.state.mode == SnapMode::Off);
   CHECK(result.state.selectionStart == 1.0 && result.state.selectionEnd == 3.0);
   first.setPriorOnEnable = true;
   result = SetSnapEnabled(firstSession, true, first);
   CHECK(result.hasState && result.state.mode == SnapMode::Prior);

   auto badApi = *realApi;
   badApi.struct_size = offsetof(CaliberApiV1, state_publication_release);
   int before = first.calls;
   SnapSession shortApiSession(101, &badApi);
   result = SetSnapEnabled(shortApiSession, true, first);
   CHECK(result.status == CALIBER_STATUS_UNSUPPORTED_VERSION && !result.applyStarted && first.calls == before);
   badApi = *realApi;
   badApi.context_take_command = nullptr;
   SnapSession missingApiSession(101, &badApi);
   result = SetSnapEnabled(missingApiSession, true, first);
   CHECK(result.status == CALIBER_STATUS_UNSUPPORTED_VERSION && !result.applyStarted);
   SnapSession zeroSession(0);
   CHECK(zeroSession.Dispatch(true) == CALIBER_STATUS_INVALID_ARGUMENT);

   auto queueApi = *realApi;
   queueApi.context_dispatch = FillAndDispatch;
   queueApi.context_destroy = CountDestroy;
   auto oldDestroys = destroys;
   SnapSession fullQueueSession(101, &queueApi);
   result = SetSnapEnabled(fullQueueSession, true, first);
   CHECK(result.status == CALIBER_STATUS_QUEUE_FULL && !result.applyStarted && first.calls == before);
   CHECK(destroys == oldDestroys + 1);

   auto takeApi = *realApi;
   takeApi.context_take_command = FailTake;
   takeApi.context_destroy = CountDestroy;
   oldDestroys = destroys;
   SnapSession failedTakeSession(101, &takeApi);
   result = SetSnapEnabled(failedTakeSession, true, first);
   CHECK(result.status == CALIBER_STATUS_INTERNAL && !result.applyStarted && first.calls == before);
   CHECK(destroys == oldDestroys + 1); // queued command discarded with context
   auto corruptCommandApi = *realApi;
   corruptCommandApi.context_take_command = CorruptCommand;
   corruptCommandApi.context_destroy = CountDestroy;
   SnapSession corruptCommandSession(101, &corruptCommandApi);
   result = SetSnapEnabled(corruptCommandSession, true, first);
   CHECK(result.status == CALIBER_STATUS_INVALID_ARGUMENT && !result.applyStarted && first.calls == before);

   auto failPublishApi = *realApi;
   failPublishApi.context_publish_state = FailPublish;
   failPublishApi.context_destroy = CountDestroy;
   first.setPriorOnEnable = false;
   SnapSession failPublishSession(101, &failPublishApi);
   result = SetSnapEnabled(failPublishSession, false, first);
   CHECK(result.status == CALIBER_STATUS_INTERNAL && result.applyStarted && !result.hasState);
   CHECK(first.calls == ++before && first.state.mode == SnapMode::Off);
   auto failReadApi = *realApi;
   failReadApi.context_read_latest_state = FailRead;
   failReadApi.context_destroy = CountDestroy;
   SnapSession failReadSession(101, &failReadApi);
   result = SetSnapEnabled(failReadSession, true, first);
   CHECK(result.status == CALIBER_STATUS_UNAVAILABLE && result.applyStarted && !result.hasState);
   CHECK(first.calls == ++before && first.state.mode == SnapMode::Nearest);
   auto corruptReadApi = *realApi;
   corruptReadApi.context_read_latest_state = CorruptRead;
   corruptReadApi.state_publication_release = CountRelease;
   corruptReadApi.context_destroy = CountDestroy;
   auto oldReleases = releases;
   SnapSession corruptReadSession(101, &corruptReadApi);
   result = SetSnapEnabled(corruptReadSession, false, first);
   CHECK(result.status == CALIBER_STATUS_INVALID_ARGUMENT && result.applyStarted && !result.hasState);
   CHECK(releases == oldReleases + 1);

   first.throws = true;
   SnapSession throwingSession(101);
   result = SetSnapEnabled(throwingSession, true, first);
   CHECK(result.status == CALIBER_STATUS_INTERNAL && result.applyStarted && !result.hasState);
   CHECK(first.state.mode == SnapMode::Nearest);
   first.throws = false;
   first.state.snapTo.assign(1025, 'x');
   SnapSession longStateSession(101);
   result = SetSnapEnabled(longStateSession, false, first);
   CHECK(result.status == CALIBER_STATUS_LIMIT_EXCEEDED && result.applyStarted && !result.hasState);
   first.state.snapTo = "seconds";
   for (int i = 0; i < 200; ++i) {
      auto& domain = i % 2 ? first : second;
      auto& session = i % 2 ? firstSession : secondSession;
      result = SetSnapEnabled(session, (i % 3) != 0, domain);
      CHECK(result.status == CALIBER_STATUS_OK && result.hasState);
      CHECK(result.state.projectId == domain.state.projectId && result.state.mode == domain.state.mode);
   }
}
}
int main()
{
   try { Run(); std::cout << "Caliber Snap bridge tests passed\n"; return 0; }
   catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
