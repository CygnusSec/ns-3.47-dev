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

struct Scenario1BaselineApplications
{
    Ptr<PacketSink> flow1Sink;
    Ptr<PacketSink> flow2Sink;
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

bool WriteScenario1BaselineMetrics(const Scenario1BaselineApplications& applications,
                                   uint32_t stationCount,
                                   uint32_t seed,
                                   uint64_t run,
                                   double trafficStartS,
                                   double simulationTimeS,
                                   const std::string& csvPath,
                                   const std::vector<uint64_t>& flow2ForwardedByNode);

bool ValidateScenario1Baseline(Ptr<FlowMonitor> monitor,
                               Ptr<Ipv4FlowClassifier> classifier,
                               uint32_t stationCount,
                               const std::vector<uint64_t>& flow2ForwardedByNode);

} // namespace ns3

#endif // SCENARIO1_BASELINE_H
