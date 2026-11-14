/**********************************************************************

  Audacity: A Digital Audio Editor

  Fade.cpp

  Dominic Mazzoni

**********************************************************************/

#include "Fade.h"
#ifdef AUDACITY_USE_ODIN_BACKEND
#include "OdinBackend.h"
#endif

Fade::Fade(bool fadeIn)
{
   mFadeIn = fadeIn;
}

Fade::~Fade()
{
}

// EffectDefinitionInterface implementation

EffectType Fade::GetType() const
{
   return EffectTypeProcess;
}

bool Fade::IsInteractive() const
{
   return false;
}

unsigned Fade::GetAudioInCount() const
{
   return 1;
}

unsigned Fade::GetAudioOutCount() const
{
   return 1;
}

bool Fade::ProcessInitialize(EffectSettings&, double, ChannelNames chanMap)
{
   mSample = 0;
   return true;
}

size_t Fade::ProcessBlock(
   EffectSettings&, const float* const* inBlock, float* const* outBlock,
   size_t blockLen)
{
#ifdef AUDACITY_USE_ODIN_BACKEND
   auto sampleIndex = mSample.as_long_long();
   const auto processed = audacity_odin_fade_process_block(
      static_cast<uint8_t>(mFadeIn), mSampleCnt.as_long_long(), &sampleIndex,
      inBlock[0], outBlock[0], blockLen);
   mSample = sampleIndex;
   return processed;
#else
   const float* ibuf = inBlock[0];
   float* obuf = outBlock[0];

   if (mFadeIn)
   {
      for (decltype(blockLen) i = 0; i < blockLen; i++)
      {
         obuf[i] = (ibuf[i] * (mSample++).as_float()) / mSampleCnt.as_float();
      }
   }
   else
   {
      for (decltype(blockLen) i = 0; i < blockLen; i++)
      {
         obuf[i] = (ibuf[i] * (mSampleCnt - 1 - mSample++).as_float()) /
                   mSampleCnt.as_float();
      }
   }

   return blockLen;
#endif
}

const ComponentInterfaceSymbol FadeIn::Symbol { XO("Fade In") };

FadeIn::FadeIn()
    : Fade { true }
{
}

ComponentInterfaceSymbol FadeIn::GetSymbol() const
{
   return Symbol;
}

TranslatableString FadeIn::GetDescription() const
{
   return XO("Applies a linear fade-in to the selected audio");
}

const ComponentInterfaceSymbol FadeOut::Symbol { XO("Fade Out") };

FadeOut::FadeOut()
    : Fade { false }
{
}

ComponentInterfaceSymbol FadeOut::GetSymbol() const
{
   return Symbol;
}

TranslatableString FadeOut::GetDescription() const
{
   return XO("Applies a linear fade-out to the selected audio");
}
