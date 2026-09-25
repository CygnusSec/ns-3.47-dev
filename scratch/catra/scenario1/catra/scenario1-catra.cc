/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "scenario1-catra.h"

namespace ns3
{

std::vector<Scenario1StationFlowCounts>
CalculateScenario1StationFlowCounts(uint32_t stationCount)
{
    std::vector<Scenario1StationFlowCounts> result(stationCount);
    std::vector<uint32_t> transmitters;
    for (uint32_t sender = 0; sender + 1 < stationCount; ++sender)
    {
        transmitters.push_back(sender); // One Flow 2 transmission per hop.
    }
    transmitters.push_back(stationCount - 2); // Flow 1 transmission from S1.

    for (uint32_t station = 0; station < stationCount; ++station)
    {
        auto& counts = result.at(station);
        for (uint32_t sender : transmitters)
        {
            const uint32_t separation = sender > station ? sender - station : station - sender;
            counts.nSend += sender == station;
            counts.nTx += separation <= 1;
            counts.nCs = counts.nCs || separation == 2;
        }
        counts.nTotal = counts.nTx + counts.nCs;
        // Paper Table 1 intentionally gives R nSEND=0, nTX=2, nCS=1.
        // TCP ACK airtime contributes to measured RBR, but the paper explicitly
        // excludes TCP ACK from competing SEND flows used in the FBR numerator.
        counts.fairBandwidthRatio = counts.nTotal == 0
                                        ? 0.0
                                        : static_cast<double>(counts.nSend) / counts.nTotal;
    }
    return result;
}

} // namespace ns3
