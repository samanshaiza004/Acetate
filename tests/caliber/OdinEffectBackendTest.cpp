/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "OdinEffectBackend.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {
void Require(bool condition, const char* message)
{
   if (!condition)
      throw message;
}

void Run()
{
   constexpr std::array<float, 4> input{1.f, 1.f, 1.f, 1.f};
   std::array<float, 4> output{};
   int64_t sampleIndex{};
   auto processed = audacity_odin_fade_process_block(
      1, 4, &sampleIndex, input.data(), output.data(), input.size());
   Require(processed == input.size(), "fade in returned the wrong count");
   Require(sampleIndex == 4, "fade in did not advance its index");
   Require(output == std::array<float, 4>{0.f, 0.25f, 0.5f, 0.75f},
      "fade in samples differ from the established formula");

   output.fill(0.f);
   sampleIndex = 0;
   processed = audacity_odin_fade_process_block(
      0, 4, &sampleIndex, input.data(), output.data(), input.size());
   Require(processed == input.size(), "fade out returned the wrong count");
   Require(sampleIndex == 4, "fade out did not advance its index");
   Require(output == std::array<float, 4>{0.75f, 0.5f, 0.25f, 0.f},
      "fade out samples differ from the established formula");

   output.fill(1.f);
   sampleIndex = 0;
   Require(audacity_odin_fade_process_block(
      1, 0, &sampleIndex, input.data(), output.data(), input.size()) == 0,
      "zero-length fade should be rejected");
   Require(sampleIndex == 0 && output == std::array<float, 4>{1.f, 1.f, 1.f, 1.f},
      "rejected fade changed its index or output");

   const std::array<float, 3> normalizeFirst{-1.f, 0.5f, 1.f};
   const std::array<float, 2> normalizeSecond{0.25f, -0.75f};
   auto sum = audacity_odin_normalize_accumulate_sum(
      normalizeFirst.data(), normalizeFirst.size(), 2.0);
   sum = audacity_odin_normalize_accumulate_sum(
      normalizeSecond.data(), normalizeSecond.size(), sum);
   Require(sum == 2.0, "Normalize DC analysis changed its double accumulator");

   std::array<float, 4> normalized{-1.f, -0.5f, 0.f, 0.5f};
   audacity_odin_normalize_process_block(
      normalized.data(), normalized.size(), 0.25f, 2.f);
   Require(normalized == std::array<float, 4>{-1.5f, -0.5f, 0.5f, 1.5f},
      "Normalize block transform changed its offset/gain formula");

   // Each block receives its own buffers. This verifies the same sample index
   // can be retained by the C++ effect object across block boundaries.
   std::array<float, 2> first{2.f, 2.f}, firstOutput{};
   std::array<float, 2> second{2.f, 2.f}, secondOutput{};
   sampleIndex = 0;
   Require(audacity_odin_fade_process_block(1, 4, &sampleIndex,
      first.data(), firstOutput.data(), first.size()) == first.size(),
      "first chunk failed");
   Require(audacity_odin_fade_process_block(1, 4, &sampleIndex,
      second.data(), secondOutput.data(), second.size()) == second.size(),
      "second chunk failed");
   Require(firstOutput == std::array<float, 2>{0.f, 0.5f} &&
      secondOutput == std::array<float, 2>{1.f, 1.5f},
      "chunked fade changed sample progression");
}
}

int main()
{
   try {
      Run();
      std::cout << "Odin effect backend tests passed\n";
      return 0;
   }
   catch (const char* message) {
      std::cerr << message << '\n';
      return 1;
   }
}
