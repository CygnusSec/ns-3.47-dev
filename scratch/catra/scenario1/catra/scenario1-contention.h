/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SCENARIO1_CONTENTION_H
#define SCENARIO1_CONTENTION_H

#include "ns3/application-container.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/nstime.h"
#include "ns3/packet-sink.h"
#include "ns3/txop.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace ns3
{

/** CSV logger for the actual ns-3 DCF CW and random backoff trace sources. */
class Scenario1CwTraceLogger
{
  public:
    Scenario1CwTraceLogger(const std::string& csvPath,
                           uint32_t stationCount,
                           bool verbose);

    void ObserveCw(uint32_t nodeIndex, Ptr<Txop> txop, uint32_t cw, uint8_t linkId);
    void ObserveBackoff(uint32_t nodeIndex,
                        Ptr<Txop> txop,
                        Time slotTime,
                        uint32_t selectedSlots,
                        uint8_t linkId);
    void PrintSummary() const;
    void BeginCatraUpdate(uint32_t nodeIndex);
    void EndCatraUpdate(uint32_t nodeIndex);

  private:
    std::ofstream m_csv;
    std::string m_csvPath;
    bool m_verbose;
    std::vector<uint32_t> m_lastCw;
    std::vector<uint64_t> m_increaseCount;
    std::vector<uint64_t> m_resetCount;
    std::vector<uint64_t> m_backoffCount;
    std::vector<uint64_t> m_catraUpdateCount;
    std::vector<uint32_t> m_maxObservedCw;
    std::vector<bool> m_cwObserved;
    std::vector<bool> m_catraUpdatePending;
};

void ObserveScenario1Cw(Scenario1CwTraceLogger* logger,
                        uint32_t nodeIndex,
                        Ptr<Txop> txop,
                        uint32_t cw,
                        uint8_t linkId);

void ObserveScenario1Backoff(Scenario1CwTraceLogger* logger,
                             uint32_t nodeIndex,
                             Ptr<Txop> txop,
                             Time slotTime,
                             uint32_t selectedSlots,
                             uint8_t linkId);

/** Add a saturated TCP sender at R targeting its adjacent station S1. */
Ptr<PacketSink> InstallScenario1TcpStressTraffic(
    const NodeContainer& nodes,
    const Ipv4InterfaceContainer& interfaces,
    double trafficStartS,
    double simulationTimeS);

constexpr uint16_t SCENARIO1_TCP_STRESS_PORT = 9100;

} // namespace ns3

#endif // SCENARIO1_CONTENTION_H
