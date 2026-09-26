/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SCENARIO1_RADIO_H
#define SCENARIO1_RADIO_H

#include <cstdint>
#include <string>
#include <vector>

namespace ns3
{

struct Scenario1StationFlowCounts
{
    uint32_t nSend{};
    uint32_t nTx{};
    uint32_t nCs{};
    uint32_t nTotal{};
    double fairBandwidthRatio{};
};

enum class Scenario1RadioRelationship
{
    DECODE,
    CCA_ONLY,
    NONE
};

struct Scenario1Transmission
{
    std::string flowName;
    uint32_t senderIndex{};
    uint32_t receiverIndex{};
};

Scenario1RadioRelationship GetScenario1RadioRelationship(double distanceM);

const char* ToString(Scenario1RadioRelationship relationship);

/** Build one channel-occupying transmission for every hop of every data flow. */
std::vector<Scenario1Transmission> BuildScenario1Transmissions(uint32_t stationCount,
                                                               bool includeTcpStress);

/** Map physical radio relationships to Algorithm 1's nSEND, nTX, nCS and FBR. */
std::vector<Scenario1StationFlowCounts> CalculateScenario1StationFlowCounts(
    const std::vector<double>& adjacentDistancesM,
    const std::vector<Scenario1Transmission>& transmissions);

} // namespace ns3

#endif // SCENARIO1_RADIO_H
