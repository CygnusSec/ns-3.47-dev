/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "catra-algorithm-packet.h"

#include "ns3/internet-module.h"
#include "ns3/llc-snap-header.h"

namespace ns3
{

ParsedTcpPacket
ParseTcpPayload(Ptr<const Packet> packet)
{
    ParsedTcpPacket parsed;
    Ptr<Packet> copy = packet->Copy();
    LlcSnapHeader llc;
    if (copy->RemoveHeader(llc) == 0 || llc.GetType() != 0x0800)
    {
        return parsed;
    }

    Ipv4Header ipv4;
    if (copy->RemoveHeader(ipv4) == 0 || ipv4.GetProtocol() != 6)
    {
        return parsed;
    }

    TcpHeader tcp;
    if (copy->RemoveHeader(tcp) == 0)
    {
        return parsed;
    }

    if (copy->GetSize() > 0)
    {
        parsed.type = ParsedTcpPacketType::DATA;
    }
    else if (tcp.GetFlags() == TcpHeader::ACK)
    {
        parsed.type = ParsedTcpPacketType::PURE_ACK;
    }
    return parsed;
}

void
ProcessAlgorithmPacket(CatraActiveTimeEstimator* estimator, const CatraAlgorithmPacket& p)
{
    if (p.destId != p.localId)
    {
        return;
    }
    if (p.macHeaderType == CatraMacHeaderType::ACK &&
        p.tcpHeaderType == ParsedTcpPacketType::DATA)
    {
        estimator->NotifyTcpTransaction(CatraTcpPacketType::DATA, p.timing);
    }
    else if (p.macHeaderType == CatraMacHeaderType::DATA &&
             p.tcpHeaderType == ParsedTcpPacketType::PURE_ACK)
    {
        estimator->NotifyTcpTransaction(CatraTcpPacketType::ACK, p.timing);
    }
}

} // namespace ns3
