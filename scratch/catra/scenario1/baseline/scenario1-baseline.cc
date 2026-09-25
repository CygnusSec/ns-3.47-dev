/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "scenario1-baseline.h"

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

constexpr uint16_t FLOW1_PORT = 5001;
constexpr uint16_t FLOW2_PORT = 5002;
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
        InetSocketAddress(Ipv4Address::GetAny(), FLOW1_PORT));
    PacketSinkHelper flow2SinkHelper(
        "ns3::TcpSocketFactory",
        InetSocketAddress(Ipv4Address::GetAny(), FLOW2_PORT));
    ApplicationContainer flow1SinkApps = flow1SinkHelper.Install(nodes.Get(receiverIndex));
    ApplicationContainer flow2SinkApps = flow2SinkHelper.Install(nodes.Get(receiverIndex));
    flow1SinkApps.Start(Seconds(0.5));
    flow2SinkApps.Start(Seconds(0.5));
    flow1SinkApps.Stop(Seconds(simulationTimeS));
    flow2SinkApps.Stop(Seconds(simulationTimeS));

    BulkSendHelper flow1("ns3::TcpSocketFactory", InetSocketAddress(receiverAddress, FLOW1_PORT));
    flow1.SetAttribute("MaxBytes", UintegerValue(0));
    flow1.SetAttribute("SendSize", UintegerValue(SCENARIO1_TCP_PAYLOAD_BYTES));
    BulkSendHelper flow2("ns3::TcpSocketFactory", InetSocketAddress(receiverAddress, FLOW2_PORT));
    flow2.SetAttribute("MaxBytes", UintegerValue(0));
    flow2.SetAttribute("SendSize", UintegerValue(SCENARIO1_TCP_PAYLOAD_BYTES));

    ApplicationContainer sources;
    sources.Add(flow1.Install(nodes.Get(s1Index)));
    sources.Add(flow2.Install(nodes.Get(0)));
    sources.Start(Seconds(trafficStartS));
    sources.Stop(Seconds(simulationTimeS));

    return {DynamicCast<PacketSink>(flow1SinkApps.Get(0)),
            DynamicCast<PacketSink>(flow2SinkApps.Get(0))};
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
    if (tcpPayload->RemoveHeader(tcp) == 0 || tcp.GetDestinationPort() != FLOW2_PORT ||
        tcpPayload->GetSize() == 0)
    {
        return;
    }
    ++forwardedByNode->at(nodeIndex);
}

bool
WriteScenario1BaselineMetrics(const Scenario1BaselineApplications& applications,
                              uint32_t stationCount,
                              uint32_t seed,
                              uint64_t run,
                              double trafficStartS,
                              double simulationTimeS,
                              const std::string& csvPath,
                              const std::vector<uint64_t>& flow2ForwardedByNode)
{
    NS_ABORT_MSG_IF(!applications.flow1Sink || !applications.flow2Sink,
                    "Baseline PacketSink lookup failed");
    const double activeS = simulationTimeS - trafficStartS;
    const uint64_t flow1Bytes = applications.flow1Sink->GetTotalRx();
    const uint64_t flow2Bytes = applications.flow2Sink->GetTotalRx();
    const double flow1Mbps = flow1Bytes * 8.0 / activeS / 1e6;
    const double flow2Mbps = flow2Bytes * 8.0 / activeS / 1e6;
    const double totalE2eMbps = flow1Mbps + flow2Mbps;
    const double paperTotalApproxMbps = flow1Mbps + (stationCount - 1) * flow2Mbps;
    const double squaredSum = flow1Mbps * flow1Mbps + flow2Mbps * flow2Mbps;
    const double jain = squaredSum > 0.0 ? totalE2eMbps * totalE2eMbps / (2.0 * squaredSum) : 0.0;
    uint64_t flow2ForwardedPackets = 0;
    std::ostringstream flow2Path;
    flow2Path << "S2(n0)";
    for (uint32_t relay = 1; relay + 1 < stationCount; ++relay)
    {
        flow2ForwardedPackets += flow2ForwardedByNode.at(relay);
        flow2Path << '>' << GetBaselineNodeRole(relay, stationCount) << "(n" << relay << ')';
    }
    flow2Path << ">R(n" << stationCount - 1 << ')';

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
              << " jain=" << jain << " active_s=" << activeS << "\n";

    const std::filesystem::path outputPath(csvPath);
    if (!outputPath.parent_path().empty())
    {
        std::filesystem::create_directories(outputPath.parent_path());
    }
    const bool writeHeader = !std::filesystem::exists(outputPath) ||
                             std::filesystem::file_size(outputPath) == 0;
    std::ofstream csv(csvPath, std::ios::app);
    NS_ABORT_MSG_IF(!csv, "Cannot open baseline CSV: " << csvPath);
    if (writeHeader)
    {
        csv << "mode,n,seed,run,tcp,active_s,flow1_mbps,flow2_mbps,total_e2e_mbps,"
               "paper_total_approx_mbps,jain,flow1_rx_bytes,flow2_rx_bytes,flow2_path,"
               "flow2_relay_nodes,flow2_forwarded_tcp_data_packets\n";
    }
    csv << std::fixed << std::setprecision(6) << "baseline," << stationCount << ',' << seed << ','
        << run << ",TcpTahoe," << activeS << ',' << flow1Mbps << ',' << flow2Mbps << ','
        << totalE2eMbps << ',' << paperTotalApproxMbps << ',' << jain << ',' << flow1Bytes << ','
        << flow2Bytes << ',' << flow2Path.str() << ',' << stationCount - 2 << ','
        << flow2ForwardedPackets << '\n';
    std::cout << "baseline_csv=" << csvPath << "\n";
    std::cout << std::defaultfloat << std::setprecision(6);
    return flow1Bytes > 0 && flow2Bytes > 0;
}

bool
ValidateScenario1Baseline(Ptr<FlowMonitor> monitor,
                          Ptr<Ipv4FlowClassifier> classifier,
                          uint32_t stationCount,
                          const std::vector<uint64_t>& flow2ForwardedByNode)
{
    struct ExpectedFlow
    {
        std::string name;
        uint16_t destinationPort;
        uint32_t expectedHops;
        bool found{false};
    };
    std::array<ExpectedFlow, 2> expected{{{"Flow1", FLOW1_PORT, 1},
                                         {"Flow2", FLOW2_PORT, stationCount - 1}}};
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
