/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "scenario1-catra.h"

#include "ns3/abort.h"

#include <cmath>

namespace ns3
{

Scenario1RadioRelationship
GetScenario1RadioRelationship(double distanceM)
{
    if (distanceM <= 250.0)
    {
        return Scenario1RadioRelationship::DECODE;
    }
    if (distanceM <= 550.0)
    {
        return Scenario1RadioRelationship::CCA_ONLY;
    }
    return Scenario1RadioRelationship::NONE;
}

const char*
ToString(Scenario1RadioRelationship relationship)
{
    switch (relationship)
    {
    case Scenario1RadioRelationship::DECODE:
        return "decode";
    case Scenario1RadioRelationship::CCA_ONLY:
        return "cca-only";
    case Scenario1RadioRelationship::NONE:
        return "none";
    }
    return "unknown";
}

std::vector<Scenario1Transmission>
BuildScenario1Transmissions(uint32_t stationCount, bool includeTcpStress)
{
    NS_ABORT_MSG_IF(stationCount < 3, "Scenario 1 requires at least three stations");
    std::vector<Scenario1Transmission> transmissions;
    for (uint32_t sender = 0; sender + 1 < stationCount; ++sender)
    {
        transmissions.push_back({"Flow2", sender, sender + 1});
    }
    transmissions.push_back({"Flow1", stationCount - 2, stationCount - 1});
    if (includeTcpStress)
    {
        transmissions.push_back({"TcpStress", stationCount - 1, stationCount - 2});
    }
    return transmissions;
}

std::vector<Scenario1StationFlowCounts>
CalculateScenario1StationFlowCounts(const std::vector<double>& adjacentDistancesM,
                                    const std::vector<Scenario1Transmission>& transmissions)
{
    const uint32_t stationCount = adjacentDistancesM.size() + 1;
    std::vector<Scenario1StationFlowCounts> result(stationCount);
    std::vector<double> positions(stationCount, 0.0);
    for (uint32_t index = 1; index < stationCount; ++index)
    {
        NS_ABORT_MSG_IF(adjacentDistancesM.at(index - 1) <= 0.0,
                        "Adjacent distances must be positive");
        positions.at(index) = positions.at(index - 1) + adjacentDistancesM.at(index - 1);
    }

    for (uint32_t station = 0; station < stationCount; ++station)
    {
        auto& counts = result.at(station);
        bool hasCarrierSenseFlow = false;
        for (const auto& transmission : transmissions)
        {
            NS_ABORT_MSG_IF(transmission.senderIndex >= stationCount ||
                                transmission.receiverIndex >= stationCount,
                            "Transmission endpoint is outside the Scenario 1 chain");
            counts.nSend += transmission.senderIndex == station;
            const double distance =
                std::abs(positions.at(transmission.senderIndex) - positions.at(station));
            const auto relationship = GetScenario1RadioRelationship(distance);
            if (relationship == Scenario1RadioRelationship::DECODE)
            {
                ++counts.nTx;
            }
            else if (relationship == Scenario1RadioRelationship::CCA_ONLY)
            {
                hasCarrierSenseFlow = true;
            }
        }
        // The paper defines nCS as a presence flag, not the number of CS flows.
        counts.nCs = hasCarrierSenseFlow ? 1 : 0;
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
