/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SIMULATION_DEBUG_HELPER_H
#define SIMULATION_DEBUG_HELPER_H

#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"

#include <string>

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
     *
     * Nodes without an IPv4 stack are skipped, so this can be enabled in
     * heterogeneous simulations.
     */
    static void EnableIpv4PacketFlowTracing();

    /**
     * Connect readable SEND, FORWARD, and DELIVER callbacks to every IPv6 node.
     *
     * Nodes without an IPv6 stack are skipped.
     */
    static void EnableIpv6PacketFlowTracing();

    /**
     * Discover and print the complete topology currently registered in NodeList.
     *
     * The report includes every node, application, device model, device address,
     * interface index, MTU, link state, channel model, and IPv4/IPv6 address.
     * It also groups devices by channel to show network links and shared media.
     *
     * @param title Optional heading used to identify the simulation.
     * @param printAttributes Print all readable TypeId attributes when true.
     */
    static void PrintTopology(const std::string& title = "ns-3 Discovered Topology",
                              bool printAttributes = true);

    /**
     * Print every node's IPv4 routing table at a scheduled simulation time.
     *
     * @param printTime Time at which routing tables should be printed.
     */
    static void PrintIpv4RoutingTablesAt(Time printTime);

  private:
    /**
     * Convert an IPv4 Protocol field into a readable name.
     */
    static std::string GetProtocolName(uint8_t protocol);

    /**
     * Print every readable TypeId attribute inherited by an object.
     */
    static void PrintObjectAttributes(Ptr<const Object> object, const std::string& indent);

    /**
     * Format one event received from an Ipv4L3Protocol trace source.
     */
    static void Ipv4PacketFlowTrace(std::string action,
                                    uint32_t nodeId,
                                    const Ipv4Header& header,
                                    Ptr<const Packet> packet,
                                    uint32_t interface);

    /**
     * Format one event received from an Ipv6L3Protocol trace source.
     */
    static void Ipv6PacketFlowTrace(std::string action,
                                    uint32_t nodeId,
                                    const Ipv6Header& header,
                                    Ptr<const Packet> packet,
                                    uint32_t interface);
};

} // namespace ns3

#endif /* SIMULATION_DEBUG_HELPER_H */
