/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "catra-tcp-controller.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ns3
{

CatraTcpDecision
CalculateCatraTcpDecision(const CatraTcpInputs& inputs,
                          double highThreshold,
                          double lowThreshold)
{
    CatraTcpDecision decision;
    decision.newCwndBytes = inputs.cwndBytes;
    if (inputs.activeTime.IsNegative() || !inputs.estimationPeriod.IsStrictlyPositive() ||
        inputs.packetCount == 0 || inputs.nTotal == 0 || inputs.segmentSizeBytes == 0 ||
        inputs.cwndBytes < inputs.segmentSizeBytes || inputs.bytesInFlight > inputs.cwndBytes ||
        lowThreshold < 0.0 || lowThreshold > 1.0 || highThreshold < 1.0 ||
        !std::isfinite(lowThreshold) || !std::isfinite(highThreshold))
    {
        return decision;
    }

    decision.fairBandwidthRatio = 1.0 / inputs.nTotal;
    decision.realBandwidthRatio =
        inputs.activeTime.GetSeconds() / inputs.estimationPeriod.GetSeconds();
    decision.ratio = decision.realBandwidthRatio / decision.fairBandwidthRatio;
    decision.averageTxTime = inputs.activeTime / inputs.packetCount;
    decision.winBytes = inputs.cwndBytes - inputs.bytesInFlight;
    decision.winPackets = static_cast<double>(decision.winBytes) / inputs.segmentSizeBytes;
    decision.fairTransmissionTime = Seconds(inputs.nTotal * decision.winPackets *
                                            decision.averageTxTime.GetSeconds());

    if (decision.ratio > highThreshold)
    {
        decision.action = CatraTcpAction::DECREASE_AND_DELAY;
        decision.newCwndBytes =
            std::max(inputs.segmentSizeBytes, inputs.cwndBytes - inputs.segmentSizeBytes);
        decision.delay = Seconds(decision.ratio * decision.fairTransmissionTime.GetSeconds());
    }
    else if (decision.ratio < lowThreshold)
    {
        decision.action = CatraTcpAction::INCREASE;
        const uint64_t increased =
            static_cast<uint64_t>(inputs.cwndBytes) + inputs.segmentSizeBytes;
        const uint64_t maximumAligned =
            std::numeric_limits<uint32_t>::max() -
            (std::numeric_limits<uint32_t>::max() % inputs.segmentSizeBytes);
        decision.newCwndBytes =
            static_cast<uint32_t>(std::min<uint64_t>(increased, maximumAligned));
    }
    else
    {
        decision.action = CatraTcpAction::ORIGINAL_TCP;
    }
    return decision;
}

const char*
ToString(CatraTcpAction action)
{
    switch (action)
    {
    case CatraTcpAction::NO_DECISION:
        return "NO_DECISION";
    case CatraTcpAction::DECREASE_AND_DELAY:
        return "DECREASE_AND_DELAY";
    case CatraTcpAction::INCREASE:
        return "INCREASE";
    case CatraTcpAction::ORIGINAL_TCP:
        return "ORIGINAL_TCP";
    }
    return "UNKNOWN";
}

} // namespace ns3
