/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "scenario1-mac-hop-measurement.h"

#include "scenario1-baseline.h"
#include "../catra/scenario1-contention.h"

#include "ns3/abort.h"
#include "ns3/ipv4-header.h"
#include "ns3/llc-snap-header.h"
#include "ns3/simulator.h"
#include "ns3/tcp-header.h"
#include "ns3/wifi-mac-header.h"
#include "ns3/wifi-mac-trailer.h"
#include "ns3/wifi-net-device.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <utility>

namespace ns3
{

Scenario1MacHopMeasurement::Scenario1MacHopMeasurement(uint32_t stationCount,
                                                       double startTimeS,
                                                       double stopTimeS,
                                                       double intervalS,
                                                       std::string csvPath,
                                                       std::string trafficProfile,
                                                       bool catraEnabled,
                                                       uint32_t seed,
                                                       uint64_t run,
                                                       std::string adjacentDistances)
    : m_stationCount(stationCount),
      m_startTimeS(startTimeS),
      m_stopTimeS(stopTimeS),
      m_intervalS(intervalS),
      m_intervalCount(intervalS > 0.0 && stopTimeS > startTimeS
                          ? static_cast<uint32_t>(
                                std::ceil((stopTimeS - startTimeS) / intervalS))
                          : 0),
      m_csvPath(std::move(csvPath)),
      m_trafficProfile(std::move(trafficProfile)),
      m_catraEnabled(catraEnabled),
      m_seed(seed),
      m_run(run),
      m_adjacentDistances(std::move(adjacentDistances)),
      m_counters(m_intervalCount * (stationCount > 0 ? stationCount - 1 : 0) * 2)
{
    NS_ABORT_MSG_IF(stationCount < 2, "MAC hop measurement requires at least two stations");
    NS_ABORT_MSG_IF(intervalS <= 0.0, "MAC hop measurement interval must be positive");
    NS_ABORT_MSG_IF(stopTimeS <= startTimeS,
                    "MAC hop measurement stop time must be after start time");
}

void
Scenario1MacHopMeasurement::RegisterDevices(const NetDeviceContainer& devices)
{
    NS_ABORT_MSG_IF(devices.GetN() != m_stationCount,
                    "MAC hop measurement device count does not match station count");
    for (uint32_t index = 0; index < devices.GetN(); ++index)
    {
        m_nodeByAddress.emplace(Mac48Address::ConvertFrom(devices.Get(index)->GetAddress()), index);
    }
}

Scenario1MacHopMeasurement::ParsedTcp
Scenario1MacHopMeasurement::ParseTcp(Ptr<const Packet> packet)
{
    ParsedTcp result;
    Ptr<Packet> copy = packet->Copy();
    LlcSnapHeader llc;
    if (copy->RemoveHeader(llc) == 0 || llc.GetType() != 0x0800)
    {
        return result;
    }
    Ipv4Header ipv4;
    if (copy->RemoveHeader(ipv4) == 0 || ipv4.GetProtocol() != 6)
    {
        return result;
    }
    TcpHeader tcp;
    if (copy->RemoveHeader(tcp) == 0)
    {
        return result;
    }

    const uint16_t sourcePort = tcp.GetSourcePort();
    const uint16_t destinationPort = tcp.GetDestinationPort();
    if (sourcePort == SCENARIO1_FLOW1_PORT || destinationPort == SCENARIO1_FLOW1_PORT)
    {
        result.flow = TcpFlow::FLOW1;
    }
    else if (sourcePort == SCENARIO1_FLOW2_PORT || destinationPort == SCENARIO1_FLOW2_PORT)
    {
        result.flow = TcpFlow::FLOW2;
    }
    else if (sourcePort == SCENARIO1_TCP_STRESS_PORT ||
             destinationPort == SCENARIO1_TCP_STRESS_PORT)
    {
        result.flow = TcpFlow::TCP_STRESS;
    }

    if (copy->GetSize() > 0)
    {
        result.kind = TcpPacketKind::DATA;
    }
    else if (tcp.GetFlags() == TcpHeader::ACK)
    {
        result.kind = TcpPacketKind::PURE_ACK;
    }
    else
    {
        result.kind = TcpPacketKind::OTHER;
    }
    return result;
}

Scenario1MacHopMeasurement::Counters&
Scenario1MacHopMeasurement::GetCounters(uint32_t intervalIndex,
                                        uint32_t hopIndex,
                                        uint32_t directionIndex)
{
    const uint32_t hopCount = m_stationCount - 1;
    return m_counters.at((intervalIndex * hopCount + hopIndex) * 2 + directionIndex);
}

void
Scenario1MacHopMeasurement::ObserveTx(uint32_t senderIndex,
                                     Ptr<WifiPhy> phy,
                                     Ptr<const Packet> packet,
                                     uint16_t,
                                     WifiTxVector txVector,
                                     MpduInfo,
                                     uint16_t)
{
    const double nowS = Simulator::Now().GetSeconds();
    if (nowS < m_startTimeS || nowS >= m_stopTimeS)
    {
        return;
    }

    Ptr<Packet> payload = packet->Copy();
    WifiMacHeader header;
    if (payload->RemoveHeader(header) == 0 || header.GetAddr1().IsGroup())
    {
        return;
    }
    const auto peer = m_nodeByAddress.find(header.GetAddr1());
    if (peer == m_nodeByAddress.end())
    {
        return;
    }
    const uint32_t receiverIndex = peer->second;
    const uint32_t separation = senderIndex > receiverIndex ? senderIndex - receiverIndex
                                                             : receiverIndex - senderIndex;
    if (separation != 1)
    {
        return;
    }

    const uint32_t intervalIndex =
        std::min(static_cast<uint32_t>((nowS - m_startTimeS) / m_intervalS),
                 m_intervalCount - 1);
    const uint32_t hopIndex = std::min(senderIndex, receiverIndex);
    const uint32_t directionIndex = senderIndex < receiverIndex ? 0 : 1;
    Counters& counters = GetCounters(intervalIndex, hopIndex, directionIndex);
    const uint64_t frameBytes = packet->GetSize();
    const double airtimeS =
        WifiPhy::CalculateTxDuration(frameBytes, txVector, phy->GetPhyBand()).GetSeconds();

    bool selected = false;
    if (header.IsData())
    {
        WifiMacTrailer trailer;
        if (payload->RemoveTrailer(trailer) == 0)
        {
            return;
        }
        const ParsedTcp parsed = ParseTcp(payload);
        if (parsed.kind == TcpPacketKind::NONE)
        {
            return;
        }
        selected = true;
        ++counters.macDataFrames;
        counters.macDataBytes += frameBytes;

        FlowCounters* flow = nullptr;
        if (parsed.flow == TcpFlow::FLOW1)
        {
            flow = &counters.flow1;
        }
        else if (parsed.flow == TcpFlow::FLOW2)
        {
            flow = &counters.flow2;
        }
        else if (parsed.flow == TcpFlow::TCP_STRESS)
        {
            flow = &counters.tcpStress;
        }

        if (flow && parsed.kind == TcpPacketKind::DATA)
        {
            ++flow->dataFrames;
            flow->dataBytes += frameBytes;
        }
        else if (flow && parsed.kind == TcpPacketKind::PURE_ACK)
        {
            ++flow->tcpAckFrames;
            flow->tcpAckBytes += frameBytes;
        }
        else
        {
            ++counters.otherTcpFrames;
            counters.otherTcpBytes += frameBytes;
        }
        if (flow && header.IsRetry())
        {
            ++flow->retryFrames;
            flow->retryBytes += frameBytes;
        }
    }
    else if (header.IsRts())
    {
        selected = true;
        ++counters.rtsFrames;
        counters.rtsBytes += frameBytes;
    }
    else if (header.IsCts())
    {
        selected = true;
        ++counters.ctsFrames;
        counters.ctsBytes += frameBytes;
    }
    else if (header.IsAck())
    {
        selected = true;
        ++counters.macAckFrames;
        counters.macAckBytes += frameBytes;
    }

    if (selected)
    {
        ++counters.totalFrames;
        counters.totalBytes += frameBytes;
        counters.txAirtimeS += airtimeS;
    }
}

void
Scenario1MacHopMeasurement::Add(Counters& destination, const Counters& source)
{
    auto addFlow = [](FlowCounters& to, const FlowCounters& from) {
        to.dataFrames += from.dataFrames;
        to.dataBytes += from.dataBytes;
        to.tcpAckFrames += from.tcpAckFrames;
        to.tcpAckBytes += from.tcpAckBytes;
        to.retryFrames += from.retryFrames;
        to.retryBytes += from.retryBytes;
    };
    addFlow(destination.flow1, source.flow1);
    addFlow(destination.flow2, source.flow2);
    addFlow(destination.tcpStress, source.tcpStress);
    destination.otherTcpFrames += source.otherTcpFrames;
    destination.otherTcpBytes += source.otherTcpBytes;
    destination.macDataFrames += source.macDataFrames;
    destination.macDataBytes += source.macDataBytes;
    destination.rtsFrames += source.rtsFrames;
    destination.rtsBytes += source.rtsBytes;
    destination.ctsFrames += source.ctsFrames;
    destination.ctsBytes += source.ctsBytes;
    destination.macAckFrames += source.macAckFrames;
    destination.macAckBytes += source.macAckBytes;
    destination.totalFrames += source.totalFrames;
    destination.totalBytes += source.totalBytes;
    destination.txAirtimeS += source.txAirtimeS;
}

void
Scenario1MacHopMeasurement::WriteCounters(std::ostream& output,
                                         const Counters& counters,
                                         double intervalDurationS)
{
    auto writeFlow = [&output](const FlowCounters& flow) {
        output << ',' << flow.dataFrames << ',' << flow.dataBytes << ',' << flow.tcpAckFrames << ','
               << flow.tcpAckBytes << ',' << flow.retryFrames << ',' << flow.retryBytes;
    };
    writeFlow(counters.flow1);
    writeFlow(counters.flow2);
    writeFlow(counters.tcpStress);
    const double macTxMbps =
        intervalDurationS > 0.0 ? counters.totalBytes * 8.0 / intervalDurationS / 1e6 : 0.0;
    const double airtimeRatio =
        intervalDurationS > 0.0 ? counters.txAirtimeS / intervalDurationS : 0.0;
    output << ',' << counters.otherTcpFrames << ',' << counters.otherTcpBytes << ','
           << counters.macDataFrames << ',' << counters.macDataBytes << ',' << counters.rtsFrames
           << ',' << counters.rtsBytes << ',' << counters.ctsFrames << ',' << counters.ctsBytes
           << ',' << counters.macAckFrames << ',' << counters.macAckBytes << ','
           << counters.totalFrames << ',' << counters.totalBytes << ',' << macTxMbps << ','
           << counters.txAirtimeS << ',' << airtimeRatio;
}

void
Scenario1MacHopMeasurement::WriteCsv() const
{
    const std::filesystem::path outputPath(m_csvPath);
    if (!outputPath.parent_path().empty())
    {
        std::filesystem::create_directories(outputPath.parent_path());
    }
    std::ofstream output(m_csvPath, std::ios::trunc);
    NS_ABORT_MSG_IF(!output, "Cannot open MAC hop time-series CSV: " << m_csvPath);
    output << "traffic_profile,catra_enabled,seed,run,n,adjacent_distances_m,interval_start_s,"
              "interval_end_s,interval_duration_s,hop,direction,"
              "flow1_data_frames,flow1_data_mac_bytes,flow1_tcp_ack_frames,"
              "flow1_tcp_ack_mac_bytes,flow1_retry_frames,flow1_retry_mac_bytes,"
              "flow2_data_frames,flow2_data_mac_bytes,flow2_tcp_ack_frames,"
              "flow2_tcp_ack_mac_bytes,flow2_retry_frames,flow2_retry_mac_bytes,"
              "tcp_stress_data_frames,tcp_stress_data_mac_bytes,tcp_stress_tcp_ack_frames,"
              "tcp_stress_tcp_ack_mac_bytes,tcp_stress_retry_frames,"
              "tcp_stress_retry_mac_bytes,other_tcp_frames,other_tcp_mac_bytes,"
              "mac_data_frames,mac_data_bytes,rts_frames,rts_bytes,cts_frames,cts_bytes,"
              "mac_ack_frames,mac_ack_bytes,total_mac_frames,total_mac_bytes,"
              "total_mac_tx_mbps,tx_airtime_s,phy_airtime_ratio\n";

    const uint32_t hopCount = m_stationCount - 1;
    output << std::fixed << std::setprecision(9);
    for (uint32_t interval = 0; interval < m_intervalCount; ++interval)
    {
        const double intervalStart = m_startTimeS + interval * m_intervalS;
        const double intervalEnd = std::min(intervalStart + m_intervalS, m_stopTimeS);
        const double duration = intervalEnd - intervalStart;
        for (uint32_t hop = 0; hop < hopCount; ++hop)
        {
            const auto writeRow = [&](const std::string& direction, const Counters& counters) {
                output << m_trafficProfile << ',' << std::boolalpha << m_catraEnabled << ','
                       << m_seed << ',' << m_run << ',' << m_stationCount << ','
                       << m_adjacentDistances << ',' << intervalStart << ',' << intervalEnd << ','
                       << duration << ",n" << hop << "-n" << hop + 1 << ',' << direction;
                WriteCounters(output, counters, duration);
                output << '\n';
            };
            const Counters& forward =
                m_counters.at((interval * hopCount + hop) * 2);
            const Counters& reverse =
                m_counters.at((interval * hopCount + hop) * 2 + 1);
            Counters both;
            Add(both, forward);
            Add(both, reverse);
            writeRow("n" + std::to_string(hop) + "->n" + std::to_string(hop + 1), forward);
            writeRow("n" + std::to_string(hop + 1) + "->n" + std::to_string(hop), reverse);
            writeRow("both", both);
        }
    }
}

void
ObserveScenario1MacHopTx(Scenario1MacHopMeasurement* measurement,
                         uint32_t senderIndex,
                         Ptr<WifiPhy> phy,
                         Ptr<const Packet> packet,
                         uint16_t channelFreqMhz,
                         WifiTxVector txVector,
                         MpduInfo mpduInfo,
                         uint16_t staId)
{
    measurement->ObserveTx(senderIndex,
                           phy,
                           packet,
                           channelFreqMhz,
                           txVector,
                           mpduInfo,
                           staId);
}

} // namespace ns3
