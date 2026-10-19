#pragma once

#include "audio/OutputLayout.h"
#include <ebur128.h>
#include <mutex>

namespace MoonwakeEbu
{
// Upstream initialization rewrites process-global energy tables. Serialize all
// library access across meter instances; this mutex is worker-only.
inline std::mutex& ebur128WorkerMutex()
{
    static std::mutex mutex;
    return mutex;
}

inline int ebuChannel (juce::AudioChannelSet::ChannelType type)
{
    using C = juce::AudioChannelSet;
    static constexpr std::pair<C::ChannelType, int> positions[] {
        { C::left, EBUR128_Mp030 }, { C::right, EBUR128_Mm030 },
        { C::centre, EBUR128_Mp000 },
        { C::LFE, EBUR128_UNUSED }, { C::LFE2, EBUR128_UNUSED },
        { C::leftSurround, EBUR128_Mp110 }, { C::rightSurround, EBUR128_Mm110 },
        { C::leftSurroundSide, EBUR128_Mp090 }, { C::rightSurroundSide, EBUR128_Mm090 },
        { C::leftSurroundRear, EBUR128_Mp135 }, { C::rightSurroundRear, EBUR128_Mm135 },
        { C::centreSurround, EBUR128_Mp180 },
        { C::leftCentre, EBUR128_MpSC }, { C::rightCentre, EBUR128_MmSC },
        { C::wideLeft, EBUR128_Mp060 }, { C::wideRight, EBUR128_Mm060 },
        { C::topMiddle, EBUR128_Tp000 },
        { C::topFrontLeft, EBUR128_Up030 }, { C::topFrontRight, EBUR128_Um030 },
        { C::topFrontCentre, EBUR128_Up000 },
        { C::topSideLeft, EBUR128_Up090 }, { C::topSideRight, EBUR128_Um090 },
        { C::topRearLeft, EBUR128_Up135 }, { C::topRearRight, EBUR128_Um135 },
        { C::topRearCentre, EBUR128_Up180 }
    };
    for (const auto& [speaker, position] : positions)
        if (speaker == type) return position;
    return -1;
}
}
