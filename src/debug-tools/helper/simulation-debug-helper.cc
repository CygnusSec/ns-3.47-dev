/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "simulation-debug-helper.h"

#include "ns3/ipv4-l3-protocol.h"
#include "ns3/ipv4-routing-helper.h"

#include <iostream>

namespace ns3
{

std::string
SimulationDebugHelper::GetProtocolName(uint8_t protocol)
{
    switch (protocol)
    {
    case 1:
        return "ICMP";
    case 6:
        return "TCP";
    case 17:
        return "UDP";
    default:
        return "IP-" + std::to_string(protocol);
    }
}

void
SimulationDebugHelper::Ipv4PacketFlowTrace(std::string action,
                                         uint32_t nodeId,
                                         const Ipv4Header& header,
                                         Ptr<const Packet> packet,
                                         uint32_t interface)
{
    std::cout << "[IPv4-FLOW]"
              << " t=" << Simulator::Now().GetSeconds() << "s"
              << " node=n" << nodeId
              << " action=" << action
              << " interface=" << interface
              << " src=" << header.GetSource()
              << " dst=" << header.GetDestination()
              << " protocol=" << GetProtocolName(header.GetProtocol())
              << " ttl=" << static_cast<uint32_t>(header.GetTtl())
              << " bytes=" << packet->GetSize() + header.GetSerializedSize() << std::endl;
}

void
SimulationDebugHelper::EnableIpv4PacketFlowTracing()
{
    std::cout << "\n"
              << "IPv4 packet-flow trace is enabled:\n"
              << "  SEND    = the source node created an IPv4 packet\n"
              << "  FORWARD = a router selected an outgoing route\n"
              << "  DELIVER = the destination node accepted the packet locally\n"
              << std::flush;

    for (auto node = NodeList::Begin(); node != NodeList::End(); ++node)
    {
        Ptr<Ipv4L3Protocol> ipv4 = (*node)->GetObject<Ipv4L3Protocol>();
        NS_ABORT_MSG_IF(!ipv4, "Every traced node must have an Ipv4L3Protocol");

        const uint32_t nodeId = (*node)->GetId();
        ipv4->TraceConnectWithoutContext(
            "SendOutgoing",
            MakeBoundCallback(&SimulationDebugHelper::Ipv4PacketFlowTrace,
                              std::string("SEND"),
                              nodeId));
        ipv4->TraceConnectWithoutContext(
            "UnicastForward",
            MakeBoundCallback(&SimulationDebugHelper::Ipv4PacketFlowTrace,
                              std::string("FORWARD"),
                              nodeId));
        ipv4->TraceConnectWithoutContext(
            "LocalDeliver",
            MakeBoundCallback(&SimulationDebugHelper::Ipv4PacketFlowTrace,
                              std::string("DELIVER"),
                              nodeId));
    }
}

void
SimulationDebugHelper::PrintPointToPointCsmaTopology(
    const NodeContainer& p2pNodes,
    const NodeContainer& csmaNodes,
    const NetDeviceContainer& p2pDevices,
    const NetDeviceContainer& csmaDevices,
    const Ipv4InterfaceContainer& p2pInterfaces,
    const Ipv4InterfaceContainer& csmaInterfaces,
    uint32_t nCsma)
{
    std::cout << "\n"
              << "================ ns-3 Second Tutorial Topology ================\n"
              << "Total unique nodes : " << nCsma + 2 << "\n"
              << "Point-to-point link: 10.1.1.0/24, 5 Mbps, 2 ms delay\n"
              << "CSMA LAN           : 10.1.2.0/24, 100 Mbps, 6560 ns delay\n"
              << "\n"
              << "Point-to-point segment\n"
              << "  n0 [Node ID " << p2pNodes.Get(0)->GetId() << "]\n"
              << "    Role       : UDP Echo client\n"
              << "    IPv4       : " << p2pInterfaces.GetAddress(0) << "\n"
              << "    Device/MAC : " << p2pDevices.Get(0)->GetAddress() << "\n"
              << "  n1 [Node ID " << p2pNodes.Get(1)->GetId() << "]\n"
              << "    Role       : router between point-to-point and CSMA\n"
              << "    P2P IPv4   : " << p2pInterfaces.GetAddress(1) << "\n"
              << "    P2P MAC    : " << p2pDevices.Get(1)->GetAddress() << "\n"
              << "    CSMA IPv4  : " << csmaInterfaces.GetAddress(0) << "\n"
              << "    CSMA MAC   : " << csmaDevices.Get(0)->GetAddress() << "\n"
              << "\n"
              << "CSMA LAN nodes\n";

    for (uint32_t i = 1; i <= nCsma; ++i)
    {
        const bool isServer = (i == nCsma);
        std::cout << "  n" << i + 1 << " [Node ID " << csmaNodes.Get(i)->GetId() << "]\n"
                  << "    Role       : "
                  << (isServer ? "UDP Echo server" : "CSMA LAN host") << "\n"
                  << "    IPv4       : " << csmaInterfaces.GetAddress(i) << "\n"
                  << "    Device/MAC : " << csmaDevices.Get(i)->GetAddress() << "\n";
    }

    std::cout << "\n"
              << "Application flow\n"
              << "  Protocol     : UDP Echo\n"
              << "  Source       : n0/" << p2pInterfaces.GetAddress(0) << "\n"
              << "  Destination  : n" << nCsma + 1 << "/"
              << csmaInterfaces.GetAddress(nCsma) << ":9\n"
              << "  Packet count : 1 request plus 1 reply\n"
              << "  Payload size : 1024 bytes\n"
              << "  IPv4 size    : 1052 bytes (1024 payload + 8 UDP + 20 IPv4)\n"
              << "  Server active: 1 s to 10 s\n"
              << "  Client active: 2 s to 10 s\n"
              << "  Expected path: n0 -> n1 (router) -> n" << nCsma + 1
              << " -> n1 (router) -> n0\n"
              << "\n"
              << "Tracing\n"
              << "  Point-to-point PCAP: second-0-0.pcap and second-1-0.pcap\n"
              << "  CSMA PCAP          : promiscuous capture on the first extra LAN node\n"
              << "===============================================================\n"
              << std::flush;
}

void
SimulationDebugHelper::PrintIpv4RoutingTablesAt(Time printTime)
{
    std::cout << "\nIPv4 routing tables will be printed at simulation time "
              << printTime.GetSeconds() << " s.\n";
    Ptr<OutputStreamWrapper> routingStream = Create<OutputStreamWrapper>(&std::cout);
    Ipv4RoutingHelper::PrintRoutingTableAllAt(printTime, routingStream);
}

void
SimulationDebugHelper::PrintUdpEchoSummary(Time finishTime,
                                         uint32_t serverNodeId,
                                         bool printRoutes,
                                         bool tracePackets)
{
    std::cout << "\n================ Simulation Summary =================\n"
              << "Finished at       : " << finishTime.GetSeconds() << " s\n"
              << "UDP Echo exchange : completed; request and reply logs are shown above\n"
              << "Request path      : n0 SEND -> n1 FORWARD -> n" << serverNodeId
              << " DELIVER\n"
              << "Reply path        : n" << serverNodeId
              << " SEND -> n1 FORWARD -> n0 DELIVER\n"
              << "Routing tables     : " << (printRoutes ? "printed" : "disabled") << "\n"
              << "IPv4 packet trace  : " << (tracePackets ? "printed above" : "disabled") << "\n"
              << "PCAP traces        : written with prefix second\n"
              << "=====================================================\n";
}

} // namespace ns3
