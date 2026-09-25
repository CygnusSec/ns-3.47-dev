/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SCENARIO1_CATRA_H
#define SCENARIO1_CATRA_H

#include <cstdint>
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

/** Map Scenario 1's fixed routes to Algorithm 1's nSEND, nTX, nCS and FBR. */
std::vector<Scenario1StationFlowCounts> CalculateScenario1StationFlowCounts(
    uint32_t stationCount);

} // namespace ns3

#endif // SCENARIO1_CATRA_H
