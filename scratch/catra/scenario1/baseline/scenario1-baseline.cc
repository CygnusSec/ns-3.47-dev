/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "scenario1-baseline.h"

#include "../catra/scenario1-contention.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/tcp-header.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace ns3
{
namespace
{

constexpr double HOP_TOLERANCE = 1e-9;

std::string
GetBaselineNodeRole(uint32_t nodeIndex, uint32_t stationCount)
{
    if (nodeIndex == 0)
    {
        return "S2-long-flow-source";
    }
    if (nodeIndex == stationCount - 2)
    {
        return "S1-short-flow-source";
    }
    if (nodeIndex == stationCount - 1)
    {
        return "R-common-receiver";
    }
    return "relay";
}

} // namespace

Scenario1BaselineApplications
InstallScenario1Baseline(const NodeContainer& nodes,
                         const Ipv4InterfaceContainer& interfaces,
                         double trafficStartS,
                         double simulationTimeS)
{
    const uint32_t s1Index = nodes.GetN() - 2;
    const uint32_t receiverIndex = nodes.GetN() - 1;
    const Ipv4Address receiverAddress = interfaces.GetAddress(receiverIndex);

    PacketSinkHelper flow1SinkHelper(
        "ns3::TcpSocketFactory",
        InetSocketAddress(Ipv4Address::GetAny(), SCENARIO1_FLOW1_PORT));
    PacketSinkHelper flow2SinkHelper(
        "ns3::TcpSocketFactory",
        InetSocketAddress(Ipv4Address::GetAny(), SCENARIO1_FLOW2_PORT));
    ApplicationContainer flow1SinkApps = flow1SinkHelper.Install(nodes.Get(receiverIndex));
    ApplicationContainer flow2SinkApps = flow2SinkHelper.Install(nodes.Get(receiverIndex));
    flow1SinkApps.Start(Seconds(0.5));
    flow2SinkApps.Start(Seconds(0.5));
    flow1SinkApps.Stop(Seconds(simulationTimeS));
    flow2SinkApps.Stop(Seconds(simulationTimeS));

    BulkSendHelper flow1("ns3::TcpSocketFactory",
                         InetSocketAddress(receiverAddress, SCENARIO1_FLOW1_PORT));
    flow1.SetAttribute("MaxBytes", UintegerValue(0));
    flow1.SetAttribute("SendSize", UintegerValue(SCENARIO1_TCP_PAYLOAD_BYTES));
    BulkSendHelper flow2("ns3::TcpSocketFactory",
                         InetSocketAddress(receiverAddress, SCENARIO1_FLOW2_PORT));
    flow2.SetAttribute("MaxBytes", UintegerValue(0));
    flow2.SetAttribute("SendSize", UintegerValue(SCENARIO1_TCP_PAYLOAD_BYTES));

    ApplicationContainer sources;
    sources.Add(flow1.Install(nodes.Get(s1Index)));
    sources.Add(flow2.Install(nodes.Get(0)));
    sources.Start(Seconds(trafficStartS));
    sources.Stop(Seconds(simulationTimeS));

    return {DynamicCast<PacketSink>(flow1SinkApps.Get(0)),
            DynamicCast<PacketSink>(flow2SinkApps.Get(0)),
            nullptr};
}

void
ObserveScenario1Flow2Forward(std::vector<uint64_t>* forwardedByNode,
                            uint32_t nodeIndex,
                            const Ipv4Header& header,
                            Ptr<const Packet> packet,
                            uint32_t)
{
    if (header.GetProtocol() != 6)
    {
        return;
    }
    Ptr<Packet> tcpPayload = packet->Copy();
    TcpHeader tcp;
    if (tcpPayload->RemoveHeader(tcp) == 0 ||
        tcp.GetDestinationPort() != SCENARIO1_FLOW2_PORT ||
        tcpPayload->GetSize() == 0)
    {
        return;
    }
    ++forwardedByNode->at(nodeIndex);
}

void
ObserveScenario1HopRx(std::vector<uint64_t>* flow1RxBytesByNode,
                     std::vector<uint64_t>* flow2RxBytesByNode,
                     uint32_t nodeIndex,
                     Ptr<const Packet> packet,
                     Ptr<Ipv4>,
                     uint32_t)
{
    Ptr<Packet> copy = packet->Copy();
    Ipv4Header ipv4;
    if (copy->RemoveHeader(ipv4) == 0 || ipv4.GetProtocol() != 6)
    {
        return;
    }
    TcpHeader tcp;
    if (copy->RemoveHeader(tcp) == 0)
    {
        return;
    }
    const uint32_t payloadBytes = copy->GetSize();
    if (payloadBytes == 0)
    {
        return; // pure TCP-ACK carries no forward-direction data
    }
    if (tcp.GetDestinationPort() == SCENARIO1_FLOW1_PORT)
    {
        flow1RxBytesByNode->at(nodeIndex) += payloadBytes;
    }
    else if (tcp.GetDestinationPort() == SCENARIO1_FLOW2_PORT)
    {
        flow2RxBytesByNode->at(nodeIndex) += payloadBytes;
    }
}

bool
WriteScenario1BaselineMetrics(const Scenario1BaselineApplications& applications,
                              uint32_t stationCount,
                              uint32_t seed,
                              uint64_t run,
                              const std::string& trafficProfile,
                              bool catraEnabled,
                              bool measurementEnabled,
                              const std::string& adjacentDistances,
                              double trafficStartS,
                              double simulationTimeS,
                              double estimationPeriodS,
                              const std::string& csvPath,
                              const std::vector<uint64_t>& flow2ForwardedByNode,
                              const std::vector<uint64_t>& flow1RxBytesByNode,
                              const std::vector<uint64_t>& flow2RxBytesByNode)
{
    NS_ABORT_MSG_IF(!applications.flow1Sink || !applications.flow2Sink,
                    "Baseline PacketSink lookup failed");
    const double activeS = simulationTimeS - trafficStartS;
    const uint64_t flow1Bytes = applications.flow1Sink->GetTotalRx();
    const uint64_t flow2Bytes = applications.flow2Sink->GetTotalRx();
    const uint64_t tcpStressBytes =
        applications.tcpStressSink ? applications.tcpStressSink->GetTotalRx() : 0;
    const double flow1Mbps = flow1Bytes * 8.0 / activeS / 1e6;
    const double flow2Mbps = flow2Bytes * 8.0 / activeS / 1e6;
    const double tcpStressActiveS = simulationTimeS - (trafficStartS + 0.1);
    const double tcpStressMbps = applications.tcpStressSink && tcpStressActiveS > 0.0
                                     ? tcpStressBytes * 8.0 / tcpStressActiveS / 1e6
                                     : 0.0;
    const double totalE2eMbps = flow1Mbps + flow2Mbps + tcpStressMbps;
    const double paperTotalApproxMbps = flow1Mbps + (stationCount - 1) * flow2Mbps;
    const double squaredSum = flow1Mbps * flow1Mbps + flow2Mbps * flow2Mbps +
                              tcpStressMbps * tcpStressMbps;
    const double flowCount = applications.tcpStressSink ? 3.0 : 2.0;
    const double jain = squaredSum > 0.0
                            ? totalE2eMbps * totalE2eMbps / (flowCount * squaredSum)
                            : 0.0;
    uint64_t flow2ForwardedPackets = 0;
    std::ostringstream flow2Path;
    flow2Path << "S2(n0)";
    for (uint32_t relay = 1; relay + 1 < stationCount; ++relay)
    {
        flow2ForwardedPackets += flow2ForwardedByNode.at(relay);
        flow2Path << '>' << GetBaselineNodeRole(relay, stationCount) << "(n" << relay << ')';
    }
    flow2Path << ">R(n" << stationCount - 1 << ')';

    // Per-hop throughput. Bytes of a flow received at node j crossed the hop
    // (j-1)->j, so hop bandwidth = bytes received at the downstream node / active
    // time. Flow2 (long) traverses every hop n0->n1->...->R; Flow1 (short)
    // occupies only the final hop n(n-2)->R. This is link-level throughput and
    // includes retransmitted TCP-DATA, so it can exceed the end-to-end goodput
    // measured at the sink (which counts unique bytes only).
    const uint32_t receiverIndex = stationCount - 1;
    const uint32_t s1Index = stationCount - 2;
    std::ostringstream flow2HopBandwidth;
    std::ostringstream flow1HopBandwidth;
    for (uint32_t hop = 0; hop < stationCount - 1; ++hop)
    {
        const uint32_t downstream = hop + 1; // node that received the hop's data
        const double hopMbps = flow2RxBytesByNode.at(downstream) * 8.0 / activeS / 1e6;
        flow2HopBandwidth << (hop == 0 ? "" : ";") << 'n' << hop << "-n" << downstream << ':'
                          << std::fixed << std::setprecision(6) << hopMbps;
    }
    const double flow1HopMbps = flow1RxBytesByNode.at(receiverIndex) * 8.0 / activeS / 1e6;
    flow1HopBandwidth << 'n' << s1Index << "-n" << receiverIndex << ':' << std::fixed
                      << std::setprecision(6) << flow1HopMbps;

    std::cout << std::fixed << std::setprecision(6)
              << "[BASELINE] flow=Flow1 path=S1->R hops=1 rx_bytes=" << flow1Bytes
              << " goodput_mbps=" << flow1Mbps << "\n"
              << "[BASELINE] flow=Flow2 path=S2->R hops=" << stationCount - 1
              << " rx_bytes=" << flow2Bytes << " goodput_mbps=" << flow2Mbps << "\n"
              << "[BASELINE] flow=Flow2 runtime_path=" << flow2Path.str()
              << " relay_nodes=" << stationCount - 2
              << " forwarded_tcp_data_packets=" << flow2ForwardedPackets << "\n"
              << "[BASELINE] total_e2e_mbps=" << totalE2eMbps
              << " paper_total_approx_mbps=" << paperTotalApproxMbps
              << " jain=" << jain << " active_s=" << activeS << "\n"
              << "[BASELINE-HOP-BW] flow=Flow2 hop_bandwidth_mbps=" << flow2HopBandwidth.str()
              << "\n"
              << "[BASELINE-HOP-BW] flow=Flow1 hop_bandwidth_mbps=" << flow1HopBandwidth.str()
              << "\n";
    if (applications.tcpStressSink)
    {
        std::cout << "[TCP-STRESS] flow=TcpStress path=R->S1 hops=1 rx_bytes="
                  << tcpStressBytes << " goodput_mbps=" << tcpStressMbps << "\n";
    }

    const std::filesystem::path outputPath(csvPath);
    if (!outputPath.parent_path().empty())
    {
        std::filesystem::create_directories(outputPath.parent_path());
    }
    const bool writeHeader = !std::filesystem::exists(outputPath) ||
                             std::filesystem::file_size(outputPath) == 0;
    const std::string csvHeader =
        "traffic_profile,catra_enabled,measurement_enabled,n,adjacent_distances_m,seed,run,"
        "tcp,sim_time_s,ep_s,contention_protocol,active_s,flow1_mbps,flow2_mbps,"
        "tcp_stress_mbps,total_e2e_mbps,paper_total_approx_mbps,jain,flow1_rx_bytes,"
        "flow2_rx_bytes,flow2_path,flow2_relay_nodes,flow2_forwarded_tcp_data_packets,"
        "flow2_hop_bandwidth_mbps,flow1_hop_bandwidth_mbps,tcp_stress_rx_bytes,"
        "tcp_stress_path,tcp_stress_hops";
    if (!writeHeader)
    {
        std::ifstream existing(csvPath);
        std::string existingHeader;
        std::getline(existing, existingHeader);
        NS_ABORT_MSG_IF(existingHeader != csvHeader,
                        "Refusing to append to throughput CSV with incompatible schema: "
                            << csvPath);
    }
    std::ofstream csv(csvPath, std::ios::app);
    NS_ABORT_MSG_IF(!csv, "Cannot open baseline CSV: " << csvPath);
    if (writeHeader)
    {
        csv << csvHeader << '\n';
    }
    csv << std::fixed << std::setprecision(6) << trafficProfile << ',' << std::boolalpha
        << catraEnabled << ',' << measurementEnabled << ',' << stationCount << ','
        << adjacentDistances << ',' << seed << ',' << run << ",CatraTcpTahoe," << simulationTimeS
        << ',' << estimationPeriodS << ','
        << (applications.tcpStressSink ? "TCP" : "none") << ',' << activeS << ',' << flow1Mbps
        << ',' << flow2Mbps << ',' << tcpStressMbps << ','
        << totalE2eMbps << ',' << paperTotalApproxMbps << ',' << jain << ',' << flow1Bytes << ','
        << flow2Bytes << ',' << flow2Path.str() << ',' << stationCount - 2 << ','
        << flow2ForwardedPackets << ',' << flow2HopBandwidth.str() << ','
        << flow1HopBandwidth.str() << ',' << tcpStressBytes << ','
        << (applications.tcpStressSink ? "R>S1" : "none") << ','
        << (applications.tcpStressSink ? 1 : 0) << '\n';
    std::cout << "baseline_csv=" << csvPath << "\n";
    std::cout << std::defaultfloat << std::setprecision(6);
    return flow1Bytes > 0 && flow2Bytes > 0;
}

bool
ValidateScenario1Baseline(Ptr<FlowMonitor> monitor,
                          Ptr<Ipv4FlowClassifier> classifier,
                          uint32_t stationCount,
                          bool tcpStressEnabled,
                          const std::vector<uint64_t>& flow2ForwardedByNode)
{
    struct ExpectedFlow
    {
        std::string name;
        uint16_t destinationPort;
        uint32_t expectedHops;
        bool found{false};
    };
    std::vector<ExpectedFlow> expected{{"Flow1", SCENARIO1_FLOW1_PORT, 1},
                                       {"Flow2", SCENARIO1_FLOW2_PORT, stationCount - 1}};
    if (tcpStressEnabled)
    {
        expected.push_back({"TcpStress", SCENARIO1_TCP_STRESS_PORT, 1});
    }
    bool passed = true;
    monitor->CheckForLostPackets();
    for (const auto& [flowId, stats] : monitor->GetFlowStats())
    {
        const Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(flowId);
        if (tuple.protocol != 6)
        {
            continue;
        }
        for (auto& flow : expected)
        {
            if (tuple.destinationPort != flow.destinationPort)
            {
                continue;
            }
            flow.found = true;
            const double observedHops = stats.rxPackets == 0
                                            ? 0.0
                                            : 1.0 + 1.0 * stats.timesForwarded / stats.rxPackets;
            const bool delivered = stats.rxPackets > 0 && stats.rxBytes > 0;
            const bool hopsPassed = std::abs(observedHops - flow.expectedHops) <= HOP_TOLERANCE;
            std::cout << "[BASELINE-FLOWMONITOR] flow=" << flow.name
                      << " src=" << tuple.sourceAddress << " dst=" << tuple.destinationAddress
                      << " tx_packets=" << stats.txPackets << " rx_packets=" << stats.rxPackets
                      << " lost_packets=" << stats.lostPackets
                      << " times_forwarded=" << stats.timesForwarded
                      << " observed_hops=" << observedHops
                      << " expected_hops=" << flow.expectedHops
                      << " delivery=" << (delivered ? "PASS" : "FAIL")
                      << " path=" << (hopsPassed ? "PASS" : "FAIL") << "\n";
            passed = passed && delivered && hopsPassed;
        }
    }
    uint64_t observedRelayForwards = 0;
    std::ostringstream path;
    path << "S2(n0)";
    for (uint32_t relay = 1; relay + 1 < stationCount; ++relay)
    {
        observedRelayForwards += flow2ForwardedByNode.at(relay);
        path << "->" << GetBaselineNodeRole(relay, stationCount) << "(n" << relay << ')';
        const bool relayPassed = flow2ForwardedByNode.at(relay) > 0;
        std::cout << "[BASELINE-FORWARD] flow=Flow2 relay_node=" << relay
                  << " role=" << GetBaselineNodeRole(relay, stationCount)
                  << " forwarded_tcp_data_packets=" << flow2ForwardedByNode.at(relay)
                  << " result=" << (relayPassed ? "PASS" : "FAIL") << "\n";
        passed = passed && relayPassed;
    }
    path << "->R(n" << stationCount - 1 << ')';
    std::cout << "[BASELINE-PATH] flow=Flow2 path=" << path.str()
              << " relay_nodes=" << stationCount - 2
              << " observed_relay_forwards=" << observedRelayForwards << "\n";
    for (const auto& flow : expected)
    {
        if (!flow.found)
        {
            std::cout << "[BASELINE-FLOWMONITOR] flow=" << flow.name
                      << " result=FAIL reason=not-found\n";
            passed = false;
        }
    }
    std::cout << "baseline_flowmonitor_overall=" << (passed ? "PASS" : "FAIL") << "\n";
    return passed;
}

} // namespace ns3
