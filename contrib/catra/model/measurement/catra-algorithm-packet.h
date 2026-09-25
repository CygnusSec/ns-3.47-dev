/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef CATRA_ALGORITHM_PACKET_H
#define CATRA_ALGORITHM_PACKET_H

#include "catra-active-time-estimator.h"

#include "ns3/mac48-address.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"

namespace ns3
{

enum class ParsedTcpPacketType
{
    DATA,
    PURE_ACK,
    OTHER
};

enum class CatraMacHeaderType
{
    ACK,
    DATA
};

struct ParsedTcpPacket
{
    ParsedTcpPacketType type{ParsedTcpPacketType::OTHER};
};

struct CatraAlgorithmPacket
{
    Mac48Address destId;
    Mac48Address localId;
    CatraMacHeaderType macHeaderType;
    ParsedTcpPacketType tcpHeaderType;
    CatraTransactionTiming timing;
};

ParsedTcpPacket ParseTcpPayload(Ptr<const Packet> packet);

void ProcessAlgorithmPacket(CatraActiveTimeEstimator* estimator,
                            const CatraAlgorithmPacket& packet);

} // namespace ns3

#endif // CATRA_ALGORITHM_PACKET_H
