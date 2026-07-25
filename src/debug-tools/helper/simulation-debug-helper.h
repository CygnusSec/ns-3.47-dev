/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SIMULATION_DEBUG_HELPER_H
#define SIMULATION_DEBUG_HELPER_H

#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"

namespace ns3
{

/**
 * Reusable console diagnostics for ns-3 simulations.
 *
 * Any program that links the debug-tools module can use this helper.
 */
class SimulationDebugHelper
{
  public:
    /**
     * Connect readable SEND, FORWARD, and DELIVER callbacks to every IPv4 node.
     */
    static void EnableIpv4PacketFlowTracing();

    /**
     * Print the point-to-point plus CSMA topology used by second.cc.
     *
     * @param p2pNodes The two nodes connected by the point-to-point link.
     * @param csmaNodes The gateway followed by all extra CSMA nodes.
     * @param p2pDevices Devices installed on the point-to-point link.
     * @param csmaDevices Devices installed on the shared CSMA LAN.
     * @param p2pInterfaces IPv4 interfaces in the point-to-point subnet.
     * @param csmaInterfaces IPv4 interfaces in the CSMA subnet.
     * @param nCsma Number of extra CSMA nodes.
     * @param serverCsmaIndex Index of the server inside csmaNodes.
     */
    static void PrintPointToPointCsmaTopology(const NodeContainer& p2pNodes,
                                              const NodeContainer& csmaNodes,
                                              const NetDeviceContainer& p2pDevices,
                                              const NetDeviceContainer& csmaDevices,
                                              const Ipv4InterfaceContainer& p2pInterfaces,
                                              const Ipv4InterfaceContainer& csmaInterfaces,
                                              uint32_t nCsma,
                                              uint32_t serverCsmaIndex);

    /**
     * Print every node's IPv4 routing table at a scheduled simulation time.
     *
     * @param printTime Time at which routing tables should be printed.
     */
    static void PrintIpv4RoutingTablesAt(Time printTime);

    /**
     * Print the final result of the tutorial UDP Echo exchange.
     *
     * @param finishTime Simulated time at which execution finished.
     * @param serverNodeId Node ID of the UDP Echo server.
     * @param printRoutes Whether routing-table output was enabled.
     * @param tracePackets Whether IPv4 packet-flow output was enabled.
     */
    static void PrintUdpEchoSummary(Time finishTime,
                                    uint32_t serverNodeId,
                                    bool printRoutes,
                                    bool tracePackets);

  private:
    /**
     * Convert an IPv4 Protocol field into a readable name.
     */
    static std::string GetProtocolName(uint8_t protocol);

    /**
     * Format one event received from an Ipv4L3Protocol trace source.
     */
    static void Ipv4PacketFlowTrace(std::string action,
                                    uint32_t nodeId,
                                    const Ipv4Header& header,
                                    Ptr<const Packet> packet,
                                    uint32_t interface);
};

} // namespace ns3

#endif /* SIMULATION_DEBUG_HELPER_H */
