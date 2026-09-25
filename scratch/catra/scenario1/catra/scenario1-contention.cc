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

constexpr uint16_t CONTENTION_PORT = 9100;
constexpr uint32_t CONTENTION_PACKET_BYTES = 1000;

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
      m_maxObservedCw(stationCount, 0)
{
    const std::filesystem::path outputPath(csvPath);
    if (!outputPath.parent_path().empty())
    {
        std::filesystem::create_directories(outputPath.parent_path());
    }
    m_csv.open(csvPath, std::ios::trunc);
    NS_ABORT_MSG_IF(!m_csv, "Cannot open CW trace CSV: " << csvPath);
    m_csv << "time_s,node,event,previous_cw_ns3,cw_ns3,cw_slots,backoff_slots,"
             "slot_time_us,backoff_time_us,reason\n";
}

void
Scenario1CwTraceLogger::ObserveCw(uint32_t nodeIndex, uint32_t cw, uint8_t)
{
    const uint32_t previous = m_lastCw.at(nodeIndex);
    std::string reason{"INITIAL_OR_UNCHANGED"};
    if (previous > 0 && cw > previous)
    {
        reason = "BEB_INCREASE_AFTER_FAILURE";
        ++m_increaseCount.at(nodeIndex);
    }
    else if (previous > 0 && cw < previous)
    {
        reason = "RESET_AFTER_SUCCESS";
        ++m_resetCount.at(nodeIndex);
    }
    m_lastCw.at(nodeIndex) = cw;
    m_maxObservedCw.at(nodeIndex) = std::max(m_maxObservedCw.at(nodeIndex), cw);

    m_csv << std::fixed << std::setprecision(9) << Simulator::Now().GetSeconds() << ','
          << nodeIndex << ",CW," << previous << ',' << cw << ',' << cw + 1
          << ",,,," << reason << '\n';
    if (m_verbose)
    {
        std::cout << "[CW-TRACE] time_s=" << Simulator::Now().GetSeconds()
                  << " node=" << nodeIndex << " previous_cw_ns3=" << previous
                  << " cw_ns3=" << cw << " cw_slots=" << cw + 1
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
          << nodeIndex << ",BACKOFF," << m_lastCw.at(nodeIndex) << ',' << cw << ',' << cw + 1
          << ',' << selectedSlots << ',' << slotTime.GetMicroSeconds() << ',' << backoffTimeUs
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
Scenario1CwTraceLogger::PrintSummary() const
{
    for (uint32_t nodeIndex = 0; nodeIndex < m_lastCw.size(); ++nodeIndex)
    {
        std::cout << "[CW-SUMMARY] node=" << nodeIndex
                  << " backoff_draws=" << m_backoffCount.at(nodeIndex)
                  << " beb_increases=" << m_increaseCount.at(nodeIndex)
                  << " success_resets=" << m_resetCount.at(nodeIndex)
                  << " max_cw_ns3=" << m_maxObservedCw.at(nodeIndex)
                  << " max_cw_slots=" << m_maxObservedCw.at(nodeIndex) + 1 << "\n";
    }
    std::cout << "cw_trace_csv=" << m_csvPath << "\n";
}

void
ObserveScenario1Cw(Scenario1CwTraceLogger* logger,
                   uint32_t nodeIndex,
                   uint32_t cw,
                   uint8_t linkId)
{
    logger->ObserveCw(nodeIndex, cw, linkId);
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

ApplicationContainer
InstallScenario1ContentionTraffic(const NodeContainer& nodes,
                                  const Ipv4InterfaceContainer& interfaces,
                                  double trafficStartS,
                                  double simulationTimeS)
{
    const uint32_t targetIndex = nodes.GetN() - 2;
    const uint32_t contenderIndex = nodes.GetN() - 1;

    PacketSinkHelper sink("ns3::UdpSocketFactory",
                          InetSocketAddress(Ipv4Address::GetAny(), CONTENTION_PORT));
    ApplicationContainer applications = sink.Install(nodes.Get(targetIndex));

    OnOffHelper contender(
        "ns3::UdpSocketFactory",
        InetSocketAddress(interfaces.GetAddress(targetIndex), CONTENTION_PORT));
    contender.SetAttribute("DataRate", DataRateValue(DataRate("11Mbps")));
    contender.SetAttribute("PacketSize", UintegerValue(CONTENTION_PACKET_BYTES));
    contender.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    contender.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    applications.Add(contender.Install(nodes.Get(contenderIndex)));
    applications.Start(Seconds(trafficStartS + 0.1));
    applications.Stop(Seconds(simulationTimeS));

    std::cout << "[CONTENTION-LOAD] enabled=true source_node=" << contenderIndex
              << " target_node=" << targetIndex << " protocol=UDP offered_rate=11Mbps"
              << " packet_bytes=" << CONTENTION_PACKET_BYTES
              << " start_s=" << trafficStartS + 0.1 << " stop_s=" << simulationTimeS << "\n";
    return applications;
}

} // namespace ns3
