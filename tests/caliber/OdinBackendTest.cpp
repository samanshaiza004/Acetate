/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "OdinBackend.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
void Require(bool condition, const char* message)
{
   if (!condition)
      throw message;
}

void ReferenceZeroCrossingScore(
   std::vector<float>& samples, std::vector<float>& scores)
{
   const auto sampleCount = samples.size();
   const auto candidateCount = scores.size();
   double previous = 2.0;
   for (size_t i = 0; i < sampleCount; ++i) {
      float score = std::fabs(samples[i]);
      if (previous * samples[i] > 0)
         score = score + 0.4;
      else if (previous > 0.0)
         score = score + 0.1;
      previous = samples[i];
      samples[i] = score;
   }

   for (size_t i = 0; i < candidateCount; ++i) {
      size_t j;
      if (candidateCount != sampleCount)
         j = i * (sampleCount - 1) / (candidateCount - 1);
      else
         j = i;
      scores[i] += samples[j];
      scores[i] += 0.1 * std::abs(int(i) - int(candidateCount / 2)) /
                   float(candidateCount / 2);
   }
}

bool ReferenceZeroCrossingOffset(
   const std::vector<float>& scores, int trackCount, int64_t& offset)
{
   int argmin = 0;
   float minimum = 3.0f;
   for (size_t i = 0; i < scores.size(); ++i) {
      if (scores[i] < minimum) {
         argmin = int(i);
         minimum = scores[i];
      }
   }
   if (trackCount == 1 && minimum > (0.2 * trackCount))
      return false;
   if (trackCount > 1 && minimum > (0.6 * trackCount))
      return false;
   offset = argmin - int(scores.size()) / 2;
   return true;
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

   std::vector<float> firstChannel{0.7f, -0.2f, 0.1f, -0.6f, 0.4f, -0.1f, 0.8f};
   std::vector<float> referenceFirst = firstChannel;
   std::vector<float> secondChannel{-0.4f, 0.8f, -0.3f, 0.2f, -0.9f};
   std::vector<float> referenceSecond = secondChannel;
   std::vector<float> scores(5, 0.f);
   std::vector<float> referenceScores(5, 0.f);
   audacity_odin_zero_crossing_score_channel(
      firstChannel.data(), firstChannel.size(), scores.data(), scores.size());
   ReferenceZeroCrossingScore(referenceFirst, referenceScores);
   audacity_odin_zero_crossing_score_channel(
      secondChannel.data(), secondChannel.size(), scores.data(), scores.size());
   ReferenceZeroCrossingScore(referenceSecond, referenceScores);
   Require(firstChannel == referenceFirst && secondChannel == referenceSecond,
      "zero-crossing channel preparation changed from the C++ reference");
   Require(scores == referenceScores,
      "zero-crossing candidate scores changed from the C++ reference");

   int64_t expectedOffset{};
   int64_t actualOffset{};
   const auto expectedFound = ReferenceZeroCrossingOffset(
      referenceScores, 2, expectedOffset);
   const auto actualFound = audacity_odin_zero_crossing_best_offset(
      scores.data(), scores.size(), 2, &actualOffset);
   Require(bool(actualFound) == expectedFound && actualOffset == expectedOffset,
      "zero-crossing best-candidate decision changed from the C++ reference");

   std::vector<float> rejectedScores{1.f, 1.f};
   expectedOffset = 0;
   actualOffset = 0;
   const auto expectedRejected = ReferenceZeroCrossingOffset(
      rejectedScores, 1, expectedOffset);
   const auto actualRejected = audacity_odin_zero_crossing_best_offset(
      rejectedScores.data(), rejectedScores.size(), 1, &actualOffset);
   Require(!expectedRejected && !actualRejected,
      "zero-crossing threshold accepted an unsuitable candidate");

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
      std::cout << "Odin backend tests passed\n";
      return 0;
   }
   catch (const char* message) {
      std::cerr << message << '\n';
      return 1;
   }
}
