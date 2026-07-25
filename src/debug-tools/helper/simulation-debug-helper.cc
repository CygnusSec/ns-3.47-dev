/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "simulation-debug-helper.h"

#include "ns3/ipv4-l3-protocol.h"
#include "ns3/ipv4-list-routing.h"
#include "ns3/ipv4-routing-helper.h"
#include "ns3/ipv6.h"
#include "ns3/ipv6-l3-protocol.h"
#include "ns3/ipv6-list-routing.h"

#include <iostream>
#include <map>
#include <set>

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
    case 58:
        return "ICMPv6";
    default:
        return "IP-" + std::to_string(protocol);
    }
}

void
SimulationDebugHelper::PrintObjectAttributes(Ptr<const Object> object,
                                             const std::string& indent)
{
    std::set<std::string> printedAttributes;
    TypeId typeId = object->GetInstanceTypeId();

    while (true)
    {
        for (std::size_t index = 0; index < typeId.GetAttributeN(); ++index)
        {
            const TypeId::AttributeInformation information = typeId.GetAttribute(index);
            if (!(information.flags & TypeId::ATTR_GET) ||
                information.supportLevel != TypeId::SupportLevel::SUPPORTED ||
                !printedAttributes.insert(information.name).second)
            {
                continue;
            }

            Ptr<AttributeValue> value = information.checker->Create();
            if (object->GetAttributeFailSafe(information.name, *value))
            {
                std::cout << indent << information.name << "="
                          << value->SerializeToString(information.checker) << "\n";
            }
        }

        if (!typeId.HasParent())
        {
            break;
        }
        typeId = typeId.GetParent();
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
        if (!ipv4)
        {
            continue;
        }

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
SimulationDebugHelper::Ipv6PacketFlowTrace(std::string action,
                                          uint32_t nodeId,
                                          const Ipv6Header& header,
                                          Ptr<const Packet> packet,
                                          uint32_t interface)
{
    std::cout << "[IPv6-FLOW]"
              << " t=" << Simulator::Now().GetSeconds() << "s"
              << " node=n" << nodeId
              << " action=" << action
              << " interface=" << interface
              << " src=" << header.GetSource()
              << " dst=" << header.GetDestination()
              << " protocol=" << GetProtocolName(header.GetNextHeader())
              << " hopLimit=" << static_cast<uint32_t>(header.GetHopLimit())
              << " bytes=" << packet->GetSize() + header.GetSerializedSize() << std::endl;
}

void
SimulationDebugHelper::EnableIpv6PacketFlowTracing()
{
    std::cout << "\n"
              << "IPv6 packet-flow trace is enabled:\n"
              << "  SEND    = the source node created an IPv6 packet\n"
              << "  FORWARD = a router selected an outgoing route\n"
              << "  DELIVER = the destination node accepted the packet locally\n"
              << std::flush;

    for (auto node = NodeList::Begin(); node != NodeList::End(); ++node)
    {
        Ptr<Ipv6L3Protocol> ipv6 = (*node)->GetObject<Ipv6L3Protocol>();
        if (!ipv6)
        {
            continue;
        }

        const uint32_t nodeId = (*node)->GetId();
        ipv6->TraceConnectWithoutContext(
            "SendOutgoing",
            MakeBoundCallback(&SimulationDebugHelper::Ipv6PacketFlowTrace,
                              std::string("SEND"),
                              nodeId));
        ipv6->TraceConnectWithoutContext(
            "UnicastForward",
            MakeBoundCallback(&SimulationDebugHelper::Ipv6PacketFlowTrace,
                              std::string("FORWARD"),
                              nodeId));
        ipv6->TraceConnectWithoutContext(
            "LocalDeliver",
            MakeBoundCallback(&SimulationDebugHelper::Ipv6PacketFlowTrace,
                              std::string("DELIVER"),
                              nodeId));
    }
}

void
SimulationDebugHelper::PrintTopology(const std::string& title, bool printAttributes)
{
    std::map<uint32_t, Ptr<Channel>> channels;

    std::cout << "\n"
              << "================ " << title << " ================\n"
              << "Nodes    : " << NodeList::GetNNodes() << "\n";

    for (auto iterator = NodeList::Begin(); iterator != NodeList::End(); ++iterator)
    {
        const Ptr<Node> node = *iterator;
        const Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        const Ptr<Ipv6> ipv6 = node->GetObject<Ipv6>();

        std::cout << "\nNode n" << node->GetId() << "\n"
                  << "  System ID    : " << node->GetSystemId() << "\n"
                  << "  Applications : " << node->GetNApplications() << "\n"
                  << "  Devices      : " << node->GetNDevices() << "\n";

        if (ipv4 && ipv4->GetRoutingProtocol())
        {
            const Ptr<Ipv4RoutingProtocol> routing = ipv4->GetRoutingProtocol();
            std::cout << "  IPv4 routing: " << routing->GetInstanceTypeId().GetName() << "\n";
            const Ptr<Ipv4ListRouting> list = DynamicCast<Ipv4ListRouting>(routing);
            if (list)
            {
                for (uint32_t index = 0; index < list->GetNRoutingProtocols(); ++index)
                {
                    int16_t priority;
                    const Ptr<Ipv4RoutingProtocol> protocol =
                        list->GetRoutingProtocol(index, priority);
                    std::cout << "    priority=" << priority
                              << " protocol=" << protocol->GetInstanceTypeId().GetName() << "\n";
                }
            }
        }
        if (ipv6 && ipv6->GetRoutingProtocol())
        {
            const Ptr<Ipv6RoutingProtocol> routing = ipv6->GetRoutingProtocol();
            std::cout << "  IPv6 routing: " << routing->GetInstanceTypeId().GetName() << "\n";
            const Ptr<Ipv6ListRouting> list = DynamicCast<Ipv6ListRouting>(routing);
            if (list)
            {
                for (uint32_t index = 0; index < list->GetNRoutingProtocols(); ++index)
                {
                    int16_t priority;
                    const Ptr<Ipv6RoutingProtocol> protocol =
                        list->GetRoutingProtocol(index, priority);
                    std::cout << "    priority=" << priority
                              << " protocol=" << protocol->GetInstanceTypeId().GetName() << "\n";
                }
            }
        }

        for (uint32_t applicationIndex = 0; applicationIndex < node->GetNApplications();
             ++applicationIndex)
        {
            const Ptr<Application> application = node->GetApplication(applicationIndex);
            std::cout << "    app[" << applicationIndex
                      << "] model=" << application->GetInstanceTypeId().GetName() << "\n";
            if (printAttributes)
            {
                PrintObjectAttributes(application, "      attribute.");
            }
        }

        for (uint32_t deviceIndex = 0; deviceIndex < node->GetNDevices(); ++deviceIndex)
        {
            const Ptr<NetDevice> device = node->GetDevice(deviceIndex);
            const Ptr<Channel> channel = device->GetChannel();

            std::cout << "    dev[" << deviceIndex << "]"
                      << " ifIndex=" << device->GetIfIndex()
                      << " model=" << device->GetInstanceTypeId().GetName()
                      << " address=" << device->GetAddress()
                      << " mtu=" << device->GetMtu()
                      << " link=" << (device->IsLinkUp() ? "up" : "down");

            if (channel)
            {
                channels[channel->GetId()] = channel;
                std::cout << " channel=ch" << channel->GetId() << " ("
                          << channel->GetInstanceTypeId().GetName() << ")";
            }
            else
            {
                std::cout << " channel=none (virtual, loopback, or unattached device)";
            }
            std::cout << "\n";

            if (printAttributes)
            {
                PrintObjectAttributes(device, "      attribute.");
            }

            if (ipv4)
            {
                const int32_t interface = ipv4->GetInterfaceForDevice(device);
                if (interface >= 0)
                {
                    for (uint32_t addressIndex = 0;
                         addressIndex < ipv4->GetNAddresses(interface);
                         ++addressIndex)
                    {
                        const Ipv4InterfaceAddress address =
                            ipv4->GetAddress(interface, addressIndex);
                        std::cout << "      IPv4[" << interface << ":" << addressIndex
                                  << "]=" << address.GetLocal()
                                  << " mask=" << address.GetMask()
                                  << " broadcast=" << address.GetBroadcast() << "\n";
                    }
                }
            }

            if (ipv6)
            {
                const int32_t interface = ipv6->GetInterfaceForDevice(device);
                if (interface >= 0)
                {
                    for (uint32_t addressIndex = 0;
                         addressIndex < ipv6->GetNAddresses(interface);
                         ++addressIndex)
                    {
                        const Ipv6InterfaceAddress address =
                            ipv6->GetAddress(interface, addressIndex);
                        std::cout << "      IPv6[" << interface << ":" << addressIndex
                                  << "]=" << address.GetAddress()
                                  << " prefix=" << address.GetPrefix() << "\n";
                    }
                }
            }
        }
    }

    std::cout << "\nChannels : " << channels.size() << "\n";
    for (const auto& [channelId, channel] : channels)
    {
        std::cout << "  ch" << channelId
                  << " model=" << channel->GetInstanceTypeId().GetName()
                  << " endpoints=" << channel->GetNDevices() << "\n";
        if (printAttributes)
        {
            PrintObjectAttributes(channel, "    attribute.");
        }
        for (std::size_t endpoint = 0; endpoint < channel->GetNDevices(); ++endpoint)
        {
            const Ptr<NetDevice> device = channel->GetDevice(endpoint);
            const Ptr<Node> node = device ? device->GetNode() : nullptr;
            std::cout << "    [" << endpoint << "] ";
            if (node && device)
            {
                std::cout << "n" << node->GetId()
                          << "/dev" << device->GetIfIndex()
                          << " address=" << device->GetAddress();
            }
            else
            {
                std::cout << "unattached";
            }
            std::cout << "\n";
        }
    }
    std::cout << "===============================================================\n"
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

} // namespace ns3
