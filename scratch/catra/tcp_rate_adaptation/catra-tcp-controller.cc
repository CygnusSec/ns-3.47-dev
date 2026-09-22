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
    if (!(inputs.fairBandwidthRatio > 0.0) || !std::isfinite(inputs.fairBandwidthRatio) ||
        inputs.realBandwidthRatio < 0.0 || !std::isfinite(inputs.realBandwidthRatio) ||
        inputs.packetCount == 0 || inputs.nTotal == 0 || inputs.segmentSizeBytes == 0 ||
        inputs.cwndBytes < inputs.segmentSizeBytes || lowThreshold < 0.0 ||
        highThreshold < lowThreshold)
    {
        return decision;
    }

    decision.ratio = inputs.realBandwidthRatio / inputs.fairBandwidthRatio;
    decision.averageTxTime = inputs.activeTime / inputs.packetCount;
    const int64_t outstandingOffset = static_cast<int64_t>(inputs.highestAckBytes) -
                                      static_cast<int64_t>(inputs.currentSequenceBytes);
    const int64_t winBytes = static_cast<int64_t>(inputs.cwndBytes) + outstandingOffset;
    decision.winBytes = static_cast<uint64_t>(std::max<int64_t>(0, winBytes));
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
        decision.newCwndBytes = static_cast<uint32_t>(
            std::min<uint64_t>(increased, std::numeric_limits<uint32_t>::max()));
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
