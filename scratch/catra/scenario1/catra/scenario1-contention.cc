/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "scenario1-contention.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>

namespace ns3
{
namespace
{

constexpr uint16_t CONTENTION_PORT = SCENARIO1_TCP_STRESS_PORT;
constexpr uint32_t CONTENTION_PACKET_BYTES = 1024;

} // namespace

Scenario1CwTraceLogger::Scenario1CwTraceLogger(const std::string& csvPath,
                                               uint32_t stationCount,
                                               bool verbose)
    : m_csvPath(csvPath),
      m_verbose(verbose),
      m_lastCw(stationCount, 0),
      m_increaseCount(stationCount, 0),
      m_resetCount(stationCount, 0),
      m_backoffCount(stationCount, 0),
      m_catraUpdateCount(stationCount, 0),
      m_maxObservedCw(stationCount, 0),
      m_cwObserved(stationCount, false),
      m_catraUpdatePending(stationCount, false)
{
    const std::filesystem::path outputPath(csvPath);
    if (!outputPath.parent_path().empty())
    {
        std::filesystem::create_directories(outputPath.parent_path());
    }
    m_csv.open(csvPath, std::ios::trunc);
    NS_ABORT_MSG_IF(!m_csv, "Cannot open CW trace CSV: " << csvPath);
    m_csv << "time_s,node,event,source,previous_cw_ns3,cw_ns3,cw_slots,cw_min_ns3,"
             "cw_max_ns3,backoff_slots,slot_time_us,backoff_time_us,reason\n";
}

void
Scenario1CwTraceLogger::ObserveCw(uint32_t nodeIndex, Ptr<Txop> txop, uint32_t cw, uint8_t)
{
    const uint32_t previous = m_lastCw.at(nodeIndex);
    std::string reason{"INITIAL_OR_UNCHANGED"};
    std::string source{"DCF"};
    if (!m_cwObserved.at(nodeIndex))
    {
        m_cwObserved.at(nodeIndex) = true;
    }
    else if (m_catraUpdatePending.at(nodeIndex))
    {
        source = "CATRA";
        reason = "CATRA_EP_UPDATE";
        ++m_catraUpdateCount.at(nodeIndex);
    }
    else if (cw > previous)
    {
        reason = "DCF_BEB_INCREASE_AFTER_FAILURE";
        ++m_increaseCount.at(nodeIndex);
    }
    else if (cw < previous)
    {
        reason = "DCF_RESET_AFTER_SUCCESS";
        ++m_resetCount.at(nodeIndex);
    }
    m_lastCw.at(nodeIndex) = cw;
    m_maxObservedCw.at(nodeIndex) = std::max(m_maxObservedCw.at(nodeIndex), cw);

    m_csv << std::fixed << std::setprecision(9) << Simulator::Now().GetSeconds() << ','
          << nodeIndex << ",CW," << source << ',' << previous << ',' << cw << ',' << cw + 1
          << ',' << txop->GetMinCw(0) << ',' << txop->GetMaxCw(0) << ",,,," << reason << '\n';
    if (m_verbose)
    {
        std::cout << "[CW-TRACE] time_s=" << Simulator::Now().GetSeconds()
                  << " node=" << nodeIndex << " previous_cw_ns3=" << previous
                  << " cw_ns3=" << cw << " cw_slots=" << cw + 1
                  << " source=" << source
                  << " reason=" << reason << "\n";
    }
}

void
Scenario1CwTraceLogger::ObserveBackoff(uint32_t nodeIndex,
                                      Ptr<Txop> txop,
                                      Time slotTime,
                                      uint32_t selectedSlots,
                                      uint8_t)
{
    const uint32_t cw = txop->GetCw(0);
    const int64_t backoffTimeUs = (selectedSlots * slotTime).GetMicroSeconds();
    ++m_backoffCount.at(nodeIndex);
    m_maxObservedCw.at(nodeIndex) = std::max(m_maxObservedCw.at(nodeIndex), cw);
    m_csv << std::fixed << std::setprecision(9) << Simulator::Now().GetSeconds() << ','
          << nodeIndex << ",BACKOFF,DCF," << m_lastCw.at(nodeIndex) << ',' << cw << ',' << cw + 1
          << ',' << txop->GetMinCw(0) << ',' << txop->GetMaxCw(0) << ',' << selectedSlots << ','
          << slotTime.GetMicroSeconds() << ',' << backoffTimeUs
          << ",UNIFORM_INTEGER_0_TO_CW\n";
    if (m_verbose)
    {
        std::cout << "[BACKOFF-TRACE] time_s=" << Simulator::Now().GetSeconds()
                  << " node=" << nodeIndex << " cw_ns3=" << cw
                  << " selected_slots=" << selectedSlots
                  << " slot_time_us=" << slotTime.GetMicroSeconds()
                  << " backoff_time_us=" << backoffTimeUs << "\n";
    }
}

void
Scenario1CwTraceLogger::BeginCatraUpdate(uint32_t nodeIndex)
{
    m_catraUpdatePending.at(nodeIndex) = true;
}

void
Scenario1CwTraceLogger::EndCatraUpdate(uint32_t nodeIndex)
{
    m_catraUpdatePending.at(nodeIndex) = false;
}

void
Scenario1CwTraceLogger::PrintSummary() const
{
    for (uint32_t nodeIndex = 0; nodeIndex < m_lastCw.size(); ++nodeIndex)
    {
        std::cout << "[CW-SUMMARY] node=" << nodeIndex
                  << " backoff_draws=" << m_backoffCount.at(nodeIndex)
                  << " beb_increases=" << m_increaseCount.at(nodeIndex)
                  << " success_resets=" << m_resetCount.at(nodeIndex)
                  << " catra_updates=" << m_catraUpdateCount.at(nodeIndex)
                  << " max_cw_ns3=" << m_maxObservedCw.at(nodeIndex)
                  << " max_cw_slots=" << m_maxObservedCw.at(nodeIndex) + 1 << "\n";
    }
    std::cout << "cw_trace_csv=" << m_csvPath << "\n";
}

void
ObserveScenario1Cw(Scenario1CwTraceLogger* logger,
                   uint32_t nodeIndex,
                   Ptr<Txop> txop,
                   uint32_t cw,
                   uint8_t linkId)
{
    logger->ObserveCw(nodeIndex, txop, cw, linkId);
}

void
ObserveScenario1Backoff(Scenario1CwTraceLogger* logger,
                        uint32_t nodeIndex,
                        Ptr<Txop> txop,
                        Time slotTime,
                        uint32_t selectedSlots,
                        uint8_t linkId)
{
    logger->ObserveBackoff(nodeIndex, txop, slotTime, selectedSlots, linkId);
}

Ptr<PacketSink>
InstallScenario1TcpStressTraffic(const NodeContainer& nodes,
                                 const Ipv4InterfaceContainer& interfaces,
                                 double trafficStartS,
                                 double simulationTimeS)
{
    const uint32_t targetIndex = nodes.GetN() - 2;
    const uint32_t contenderIndex = nodes.GetN() - 1;

    PacketSinkHelper sink("ns3::TcpSocketFactory",
                          InetSocketAddress(Ipv4Address::GetAny(), CONTENTION_PORT));
    ApplicationContainer sinkApplications = sink.Install(nodes.Get(targetIndex));
    sinkApplications.Start(Seconds(0.5));
    sinkApplications.Stop(Seconds(simulationTimeS));

    BulkSendHelper contender("ns3::TcpSocketFactory",
                             InetSocketAddress(interfaces.GetAddress(targetIndex), CONTENTION_PORT));
    contender.SetAttribute("MaxBytes", UintegerValue(0));
    contender.SetAttribute("SendSize", UintegerValue(CONTENTION_PACKET_BYTES));
    ApplicationContainer sourceApplications = contender.Install(nodes.Get(contenderIndex));
    sourceApplications.Start(Seconds(trafficStartS + 0.1));
    sourceApplications.Stop(Seconds(simulationTimeS));

    std::cout << "[CONTENTION-LOAD] enabled=true source_node=" << contenderIndex
              << " target_node=" << targetIndex << " direction=R->S1 protocol=TCP"
              << " offered_rate=saturated"
              << " packet_bytes=" << CONTENTION_PACKET_BYTES
              << " start_s=" << trafficStartS + 0.1 << " stop_s=" << simulationTimeS << "\n";
    return DynamicCast<PacketSink>(sinkApplications.Get(0));
}

} // namespace ns3
