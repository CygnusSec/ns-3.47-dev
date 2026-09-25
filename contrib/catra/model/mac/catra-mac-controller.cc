/* SPDX-License-Identifier: GPL-2.0-only */

#include "catra-mac-controller.h"

#include <algorithm>
#include <cmath>

namespace ns3
{

CatraMacDecision
CalculateCatraMacDecision(uint32_t currentNs3Cw,
                          uint32_t maximumNs3Cw,
                          double fairBandwidthRatio,
                          double realBandwidthRatio)
{
    CatraMacDecision result;
    const uint32_t currentWindow = currentNs3Cw + 1;
    result.windowSlots = currentWindow;
    result.ns3Cw = currentNs3Cw;
    if (!(fairBandwidthRatio > 0.0) || !std::isfinite(fairBandwidthRatio) ||
        realBandwidthRatio < 0.0 || !std::isfinite(realBandwidthRatio))
    {
        return result;
    }

    result.ratio = realBandwidthRatio / fairBandwidthRatio;
    result.rawWindowSlots =
        std::min(result.ratio * currentWindow, static_cast<double>(maximumNs3Cw + 1));
    result.windowSlots = std::max<uint32_t>(1, std::llround(result.rawWindowSlots));
    result.ns3Cw = result.windowSlots - 1;
    result.action = result.windowSlots < currentWindow   ? CatraMacAction::DECREASE_CW
                    : result.windowSlots > currentWindow ? CatraMacAction::INCREASE_CW
                                                        : CatraMacAction::KEEP_CW;
    return result;
}

const char*
ToString(CatraMacAction action)
{
    switch (action)
    {
    case CatraMacAction::NO_DATA_SEND_FLOW:
        return "NO_DATA_SEND_FLOW_FBR_ZERO";
    case CatraMacAction::DECREASE_CW:
        return "DECREASE_CW";
    case CatraMacAction::KEEP_CW:
        return "KEEP_CW";
    case CatraMacAction::INCREASE_CW:
        return "INCREASE_CW";
    }
    return "UNKNOWN";
}

} // namespace ns3
