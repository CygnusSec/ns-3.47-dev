/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "observe-mac-frame.h"

#include "catra-algorithm-packet.h"

namespace ns3
{
namespace
{

CatraTransactionTiming
CalculateTransactionTiming(Ptr<WifiNetDevice> sender,
                           Ptr<WifiNetDevice> receiver,
                           Ptr<const Packet> packet)
{
    Ptr<WifiPhy> senderPhy = sender->GetPhy();
    Ptr<WifiMac> senderMac = sender->GetMac();
    Ptr<WifiRemoteStationManager> senderManager = sender->GetRemoteStationManager();
    Ptr<WifiRemoteStationManager> receiverManager = receiver->GetRemoteStationManager();
    const Mac48Address senderAddress = Mac48Address::ConvertFrom(sender->GetAddress());
    const Mac48Address receiverAddress = Mac48Address::ConvertFrom(receiver->GetAddress());
    const MHz_u channelWidth = senderPhy->GetChannelWidth();

    WifiMacHeader dataHeader;
    dataHeader.SetType(WIFI_MAC_DATA);
    dataHeader.SetAddr1(receiverAddress);
    dataHeader.SetAddr2(senderAddress);
    dataHeader.SetAddr3(receiverAddress);
    const auto mpdu = Create<WifiMpdu>(packet->Copy(), dataHeader);

    const WifiTxVector dataTxVector = senderManager->GetDataTxVector(dataHeader, channelWidth);
    const WifiTxVector rtsTxVector =
        senderManager->GetRtsTxVector(receiverAddress, channelWidth);
    const WifiTxVector ctsTxVector =
        receiverManager->GetCtsTxVector(senderAddress, rtsTxVector.GetMode());
    const WifiTxVector ackTxVector =
        receiverManager->GetAckTxVector(senderAddress, dataTxVector);

    CatraTransactionTiming timing;
    timing.contentionWindow = senderMac->GetTxop()->GetCw(0);
    timing.slotTime = senderPhy->GetSlot();
    timing.rtsTime =
        WifiPhy::CalculateTxDuration(GetRtsSize(), rtsTxVector, senderPhy->GetPhyBand());
    timing.ctsTime =
        WifiPhy::CalculateTxDuration(GetCtsSize(), ctsTxVector, senderPhy->GetPhyBand());
    timing.tcpFrameTime =
        WifiPhy::CalculateTxDuration(mpdu->GetSize(), dataTxVector, senderPhy->GetPhyBand());
    timing.macAckTime =
        WifiPhy::CalculateTxDuration(GetAckSize(), ackTxVector, senderPhy->GetPhyBand());
    timing.sifs = senderPhy->GetSifs();
    timing.difs = timing.sifs + 2 * timing.slotTime;
    return timing;
}

} // namespace

void
ObserveMacFrameRx(CatraActiveTimeEstimator* estimator,
                  CatraMacTransactionTracker* tracker,
                  Ptr<WifiNetDevice> peerSender,
                  Ptr<WifiNetDevice> localReceiver,
                  Ptr<const Packet> packet,
                  uint16_t,
                  WifiTxVector,
                  MpduInfo,
                  SignalNoiseDbm,
                  uint16_t)
{
    Ptr<Packet> payload = packet->Copy();
    WifiMacHeader macHeader;
    if (payload->RemoveHeader(macHeader) == 0)
    {
        return;
    }

    const Mac48Address localMac = Mac48Address::ConvertFrom(localReceiver->GetAddress());
    if (macHeader.GetAddr1() != localMac || !macHeader.IsData())
    {
        return;
    }

    WifiMacTrailer trailer;
    if (payload->RemoveTrailer(trailer) == 0)
    {
        return;
    }

    const ParsedTcpPacket parsed = ParseTcpPayload(payload);
    if (parsed.type == ParsedTcpPacketType::DATA)
    {
        tracker->Store(macHeader.GetAddr2(),
                       {macHeader.GetAddr1(),
                        localMac,
                        CalculateTransactionTiming(peerSender, localReceiver, payload)});
    }
    else if (parsed.type == ParsedTcpPacketType::PURE_ACK)
    {
        ProcessAlgorithmPacket(
            estimator,
            {macHeader.GetAddr1(),
             localMac,
             CatraMacHeaderType::DATA,
             ParsedTcpPacketType::PURE_ACK,
             CalculateTransactionTiming(peerSender, localReceiver, payload)});
    }
}

void
ObserveMacFrameTx(CatraActiveTimeEstimator* estimator,
                  CatraMacTransactionTracker* tracker,
                  Ptr<const Packet> packet,
                  uint16_t,
                  WifiTxVector,
                  MpduInfo,
                  uint16_t)
{
    Ptr<Packet> copy = packet->Copy();
    WifiMacHeader macHeader;
    if (copy->RemoveHeader(macHeader) == 0 || !macHeader.IsAck())
    {
        return;
    }

    const auto pending = tracker->Take(macHeader.GetAddr1());
    if (!pending)
    {
        return;
    }
    ProcessAlgorithmPacket(estimator,
                           {pending->destId,
                            pending->localId,
                            CatraMacHeaderType::ACK,
                            ParsedTcpPacketType::DATA,
                            pending->timing});
}

} // namespace ns3
