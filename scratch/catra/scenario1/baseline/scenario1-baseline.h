/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SCENARIO1_BASELINE_H
#define SCENARIO1_BASELINE_H

#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/packet-sink.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ns3
{

constexpr uint32_t SCENARIO1_TCP_PAYLOAD_BYTES = 1024;
constexpr uint16_t SCENARIO1_FLOW1_PORT = 5001;
constexpr uint16_t SCENARIO1_FLOW2_PORT = 5002;

struct Scenario1BaselineApplications
{
    Ptr<PacketSink> flow1Sink;
    Ptr<PacketSink> flow2Sink;
    Ptr<PacketSink> tcpStressSink;
};

Scenario1BaselineApplications InstallScenario1Baseline(
    const NodeContainer& nodes,
    const Ipv4InterfaceContainer& interfaces,
    double trafficStartS,
    double simulationTimeS);

void ObserveScenario1Flow2Forward(std::vector<uint64_t>* forwardedByNode,
                                 uint32_t nodeIndex,
                                 const Ipv4Header& header,
                                 Ptr<const Packet> packet,
                                 uint32_t interface);

/**
 * Count TCP-DATA payload bytes of each flow received at a node's IP layer.
 *
 * A packet observed at node j has successfully traversed the wireless hop from
 * node j-1 to node j, so accumulating received bytes per node yields per-hop
 * throughput once divided by the active duration.
 */
void ObserveScenario1HopRx(std::vector<uint64_t>* flow1RxBytesByNode,
                           std::vector<uint64_t>* flow2RxBytesByNode,
                           uint32_t nodeIndex,
                           Ptr<const Packet> packet,
                           Ptr<Ipv4> ipv4,
                           uint32_t interface);

bool WriteScenario1BaselineMetrics(const Scenario1BaselineApplications& applications,
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
                                   const std::vector<uint64_t>& flow2RxBytesByNode);

bool ValidateScenario1Baseline(Ptr<FlowMonitor> monitor,
                               Ptr<Ipv4FlowClassifier> classifier,
                               uint32_t stationCount,
                               bool tcpStressEnabled,
                               const std::vector<uint64_t>& flow2ForwardedByNode);

} // namespace ns3

#endif // SCENARIO1_BASELINE_H
