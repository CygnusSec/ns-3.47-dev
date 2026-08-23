/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// This executable validates CATRA Algorithm 1 from packets delivered to each
// local station. MacRx establishes p.destID == localID; the received MAC DATA
// payload is then classified as TCP-DATA or reverse-flow pure TCP-ACK. The
// probe reads CW and never changes channel-access state.

#include "catra-active-time-estimator.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include <iomanip>
#include <iostream>
#include <memory>
#include <set>
#include <tuple>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("CatraActiveTimeProbe");

namespace
{

void
PrintSample(const CatraActiveTimeSample& sample)
{
    // Use a stable key=value format so results remain human-readable while
    // also being easy to parse into CSV during later n=3..6 experiment runs.
    std::cout << std::fixed << std::setprecision(6)
              << "[CATRA-ACTIVE-TIME] station=" << sample.stationIndex
              << " period_start_s=" << sample.periodStart.GetSeconds()
              << " period_end_s=" << sample.periodEnd.GetSeconds()
              << " raw_active_s=" << sample.rawActiveTime.GetSeconds()
              << " previous_weight=0.800000 current_weight=0.200000"
              << " smoothed_active_s=" << sample.smoothedActiveTime.GetSeconds()
              << " rbrs=" << sample.realBandwidthRatio
              << " tcp_data_packets=" << sample.tcpDataPackets
              << " tcp_ack_packets=" << sample.tcpAckPackets << "\n";

    const double previousContribution = 0.8 * sample.previousSmoothedActiveTime.GetSeconds();
    const double currentContribution = 0.2 * sample.rawActiveTime.GetSeconds();
    const double periodDuration = (sample.periodEnd - sample.periodStart).GetSeconds();
    std::cout << "  [EP-WINDOW] interval=[" << sample.periodStart.GetSeconds() << ", "
              << sample.periodEnd.GetSeconds() << ") seconds duration_s=" << periodDuration
              << " boundary_semantics=half-open\n"
              << "  [PACKET-CLASSIFICATION] local_tcp_data=" << sample.tcpDataPackets
              << " local_pure_tcp_ack=" << sample.tcpAckPackets << "\n"
              << "  [BACKOFF-INPUT] average_sender_cw=" << sample.averageContentionWindow
              << " slot_time_s=" << sample.slotTime.GetSeconds()
              << " expected_per_packet=0.5*CW*ST\n"
              << "  [TRANSACTION-COMPONENTS] expected_backoff_s="
              << sample.expectedBackoffTime.GetSeconds()
              << " rts_s=" << sample.rtsTime.GetSeconds()
              << " cts_s=" << sample.ctsTime.GetSeconds()
              << " tcp_frame_s=" << sample.tcpFrameTime.GetSeconds()
              << " mac_ack_s=" << sample.macAckTime.GetSeconds()
              << " interframe_3sifs_plus_difs_s=" << sample.interframeTime.GetSeconds() << "\n"
              << "  [ACTIVE-TIME-SUM] t_s=" << sample.rawActiveTime.GetSeconds()
              << " formula=sum(0.5*CW*ST+RTS+SIFS+CTS+SIFS+TCP_FRAME+SIFS+MAC_ACK+DIFS)\n"
              << "  [EWMA-INPUT] previous_tactive_s="
              << sample.previousSmoothedActiveTime.GetSeconds()
              << " current_t_s=" << sample.rawActiveTime.GetSeconds() << "\n"
              << "  [EWMA-CALCULATION] (0.8 * "
              << sample.previousSmoothedActiveTime.GetSeconds() << ") + (0.2 * "
              << sample.rawActiveTime.GetSeconds() << ") = "
              << previousContribution << " + " << currentContribution << " = "
              << sample.smoothedActiveTime.GetSeconds() << " seconds\n"
              << "  [RBRS-CALCULATION] " << sample.smoothedActiveTime.GetSeconds() << " / "
              << periodDuration << " = " << sample.realBandwidthRatio
              << " dimensionless_local_tx_ratio\n";
}

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

    const WifiTxVector dataTxVector =
        senderManager->GetDataTxVector(dataHeader, channelWidth);
    const WifiTxVector rtsTxVector =
        senderManager->GetRtsTxVector(receiverAddress, channelWidth);
    const WifiTxVector ctsTxVector =
        receiverManager->GetCtsTxVector(senderAddress, rtsTxVector.GetMode());
    const WifiTxVector ackTxVector =
        receiverManager->GetAckTxVector(senderAddress, dataTxVector);

    CatraTransactionTiming timing;
    timing.contentionWindow = senderMac->GetTxop()->GetCw(0);
    timing.slotTime = senderPhy->GetSlot();
    timing.rtsTime = WifiPhy::CalculateTxDuration(
        GetRtsSize(), rtsTxVector, senderPhy->GetPhyBand());
    timing.ctsTime = WifiPhy::CalculateTxDuration(
        GetCtsSize(), ctsTxVector, senderPhy->GetPhyBand());
    timing.tcpFrameTime = WifiPhy::CalculateTxDuration(
        mpdu->GetSize(), dataTxVector, senderPhy->GetPhyBand());
    timing.macAckTime = WifiPhy::CalculateTxDuration(
        GetAckSize(), ackTxVector, senderPhy->GetPhyBand());
    timing.sifs = senderPhy->GetSifs();
    timing.difs = timing.sifs + 2 * timing.slotTime;
    return timing;
}

enum class ParsedTcpPacketType
{
    DATA,
    PURE_ACK,
    OTHER
};

struct CatraFlowKey
{
    Ipv4Address source;
    Ipv4Address destination;
    uint16_t sourcePort;
    uint16_t destinationPort;

    auto Tie() const
    {
        return std::tie(source, destination, sourcePort, destinationPort);
    }

    bool operator<(const CatraFlowKey& other) const
    {
        return Tie() < other.Tie();
    }

    CatraFlowKey Reverse() const
    {
        return {destination, source, destinationPort, sourcePort};
    }
};

struct ParsedTcpPacket
{
    ParsedTcpPacketType type{ParsedTcpPacketType::OTHER};
    CatraFlowKey flow{};
};

ParsedTcpPacket
ParseLocalTcpPacket(Ptr<const Packet> packet, Ipv4Address localAddress)
{
    ParsedTcpPacket parsed;
    Ptr<Packet> copy = packet->Copy();
    LlcSnapHeader llc;
    if (copy->RemoveHeader(llc) == 0 || llc.GetType() != 0x0800)
    {
        return parsed;
    }

    Ipv4Header ipv4;
    if (copy->RemoveHeader(ipv4) == 0 || ipv4.GetProtocol() != 6 ||
        ipv4.GetDestination() != localAddress)
    {
        return parsed;
    }

    TcpHeader tcp;
    if (copy->RemoveHeader(tcp) == 0)
    {
        return parsed;
    }

    parsed.flow = {ipv4.GetSource(),
                   ipv4.GetDestination(),
                   tcp.GetSourcePort(),
                   tcp.GetDestinationPort()};
    const bool tcpData = copy->GetSize() > 0;
    const bool pureTcpAck = copy->GetSize() == 0 && tcp.GetFlags() == TcpHeader::ACK;
    if (tcpData)
    {
        parsed.type = ParsedTcpPacketType::DATA;
    }
    else if (pureTcpAck)
    {
        parsed.type = ParsedTcpPacketType::PURE_ACK;
    }
    return parsed;
}

void
ObserveLocalTcpPacket(CatraActiveTimeEstimator* estimator,
                      std::set<CatraFlowKey>* dataFlows,
                      Ptr<WifiNetDevice> peerSender,
                      Ptr<WifiNetDevice> localReceiver,
                      Ipv4Address localAddress,
                      Ptr<const Packet> packet)
{
    // MacRx is non-promiscuous, and the explicit IPv4 comparison implements
    // Algorithm 1's outer p.destID == localID condition. A successfully
    // received unicast TCP-DATA MPDU causes the local 802.11 MAC to send the
    // MAC ACK represented by the first branch of the paper.
    const ParsedTcpPacket parsed = ParseLocalTcpPacket(packet, localAddress);
    if (parsed.type == ParsedTcpPacketType::OTHER)
    {
        return;
    }

    CatraTcpPacketType packetType;
    if (parsed.type == ParsedTcpPacketType::DATA)
    {
        dataFlows->insert(parsed.flow);
        packetType = CatraTcpPacketType::DATA;
    }
    else
    {
        // A header-only ACK is a data-transfer TCP-ACK only after a TCP-DATA
        // packet has established the opposite flow direction. This rejects the
        // final ACK of the TCP three-way handshake at the receiver station.
        if (!dataFlows->contains(parsed.flow.Reverse()))
        {
            return;
        }
        packetType = CatraTcpPacketType::ACK;
    }

    const CatraTransactionTiming timing =
        CalculateTransactionTiming(peerSender, localReceiver, packet);
    estimator->NotifyTcpTransaction(packetType, timing);
}

} // namespace

int
main(int argc, char* argv[])
{
    // EP=2 seconds and the 0.8 history weight below are paper parameters. The
    // remaining values only control this short validation workload.
    double estimationPeriodS{2.0};
    double simulationTimeS{8.0};
    uint32_t packetSize{1024};
    bool strict{true};

    CommandLine cmd(__FILE__);
    cmd.AddValue("ep", "CATRA estimation period in seconds", estimationPeriodS);
    cmd.AddValue("simulationTime", "Probe stop time in seconds", simulationTimeS);
    cmd.AddValue("packetSize", "TCP segment and BulkSend write size", packetSize);
    cmd.AddValue("strict", "Fail unless TCP DATA/ACK classification and read-only CW pass", strict);
    cmd.Parse(argc, argv);

    NS_ABORT_MSG_IF(estimationPeriodS <= 0.0, "ep must be positive");
    NS_ABORT_MSG_IF(simulationTimeS <= estimationPeriodS, "simulationTime must exceed ep");

    std::cout << "\n=== CATRA Algorithm 1 active-time probe ===\n"
              << "mode=paper-algorithm-1 cw_control=read-only "
                 "packet_classifier=local-destination-tcp-data-or-reverse-flow-tcp-ack\n"
              << "measurement=modeled-complete-wifi-transaction-time ep_s=" << estimationPeriodS
              << " smoothing=0.8*previous+0.2*current\n"
              << "[ALGORITHM-MAPPING] MacRx plus IPv4 destination parsing implements "
                 "p.destID=localID; received TCP-DATA implies the local MAC ACK response; "
                 "reverse-flow pure ACK maps TCPACK; CW is read only.\n";

    // The probe deliberately uses the smallest useful topology: node 0 sends
    // application traffic and node 1 receives it and transmits Wi-Fi control
    // responses. This exercises independent estimator state at both stations.
    NodeContainer nodes;
    nodes.Create(2);

    // MobilityHelper assigns both the initial coordinates and the mobility
    // model to every probe node. Although these stations never move, ns-3's
    // propagation models still require each node to own a MobilityModel so the
    // channel can calculate distance-dependent receive power and delay.
    MobilityHelper mobility;

    // GridPositionAllocator generates positions from a regular two-dimensional
    // grid. With GridWidth=2 and RowFirst ordering, the first two allocations
    // are:
    //
    //   node 0 -> (MinX,          MinY) = ( 0 m, 0 m)
    //   node 1 -> (MinX + DeltaX, MinY) = (10 m, 0 m)
    //
    // DeltaY is zero because this probe uses one horizontal row. It would only
    // affect node 2 and later nodes after the allocator wrapped to a new row;
    // this executable creates exactly two nodes, so no row wrap occurs.
    mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                  // X coordinate of the first allocated station.
                                  "MinX", DoubleValue(0.0),
                                  // Y coordinate of the first allocated station.
                                  "MinY", DoubleValue(0.0),
                                  // Horizontal separation between consecutive grid columns.
                                  // The 10 m distance intentionally produces a reliable link,
                                  // isolating estimator validation from propagation failures.
                                  // It is probe-only and does not replace the paper's 200 m
                                  // spacing used by the real Scenario 1 topology.
                                  "DeltaX", DoubleValue(10.0),
                                  // Vertical separation between grid rows; unused for two nodes.
                                  "DeltaY", DoubleValue(0.0),
                                  // Place two nodes in each row before advancing to another row.
                                  "GridWidth", UintegerValue(2),
                                  // Fill each row from left to right before starting the next one.
                                  "LayoutType", StringValue("RowFirst"));

    // ConstantPositionMobilityModel preserves the coordinates assigned above
    // for the entire simulation. This removes movement as a source of changing
    // packet delivery behavior, allowing the probe to focus on Algorithm 1.
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

    // Install one configured mobility model on every node in the container and
    // consume positions from GridPositionAllocator in NodeContainer order.
    mobility.Install(nodes);

    std::cout << "\n=== Mobility and topology ===\n"
              << "[MOBILITY-CONFIG] allocator=GridPositionAllocator model="
                 "ConstantPositionMobilityModel min_x_m=0 min_y_m=0 delta_x_m=10 "
                 "delta_y_m=0 grid_width=2 layout=RowFirst moving=false\n";
    const Vector referencePosition = nodes.Get(0)->GetObject<MobilityModel>()->GetPosition();
    for (uint32_t index = 0; index < nodes.GetN(); ++index)
    {
        Ptr<MobilityModel> model = nodes.Get(index)->GetObject<MobilityModel>();
        const Vector position = model->GetPosition();
        const Vector velocity = model->GetVelocity();
        const double distanceFromStation0 = CalculateDistance(referencePosition, position);
        std::cout << std::fixed << std::setprecision(3)
                  << "[MOBILITY-NODE] station=" << index
                  << " node_id=" << nodes.Get(index)->GetId()
                  << " role=" << (index == 0 ? "tcp-source" : "tcp-receiver")
                  << " model=ConstantPositionMobilityModel"
                  << " position_m=(" << position.x << "," << position.y << "," << position.z
                  << ") velocity_mps=(" << velocity.x << "," << velocity.y << "," << velocity.z
                  << ") distance_from_station0_m=" << distanceFromStation0 << "\n";
    }
    std::cout << "[MOBILITY-EFFECT] positions remain constant; distance controls propagation "
                 "delay/loss, while mobility contributes no time variation to packet delivery.\n";

    // This executable validates trace accounting, not the calibrated Scenario
    // 1 propagation boundaries. The default Yans channel and a reliable 10 m
    // link remove propagation loss as a confounding variable.
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());

    // Match the paper's legacy 802.11b data rate. Rate adaptation is disabled
    // so a change in manager-selected modulation cannot alter TX durations.
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("DsssRate11Mbps"),
                                 "ControlMode", StringValue("DsssRate11Mbps"),
                                 "RtsCtsThreshold", UintegerValue(0));
    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);

    // IPv4 is required only to generate repeatable application traffic for the
    // PHY trace. No CATRA algorithm reads IP state in this probe.
    InternetStackHelper internet;
    internet.Install(nodes);
    Ipv4AddressHelper addresses;
    addresses.SetBase("10.50.0.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = addresses.Assign(devices);
    for (uint32_t index = 0; index < nodes.GetN(); ++index)
    {
        std::cout << "[NETWORK-NODE] station=" << index
                  << " ipv4=" << interfaces.GetAddress(index)
                  << " wifi_standard=802.11b mac=adhoc data_mode=DsssRate11Mbps"
                  << " control_mode=DsssRate11Mbps\n";
    }

    // Use TCP because Algorithm 1 explicitly classifies TCP-DATA and TCP-ACK.
    // Start the sink before BulkSend; the long-lived saturated connection makes
    // several complete EPs and their EWMA history visible in the logs.
    const uint16_t port = 9000;
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(packetSize));
    PacketSinkHelper sink("ns3::TcpSocketFactory",
                          InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApp = sink.Install(nodes.Get(1));
    sinkApp.Start(Seconds(0.5));
    sinkApp.Stop(Seconds(simulationTimeS));

    BulkSendHelper sender("ns3::TcpSocketFactory",
                          InetSocketAddress(interfaces.GetAddress(1), port));
    sender.SetAttribute("SendSize", UintegerValue(packetSize));
    sender.SetAttribute("MaxBytes", UintegerValue(0));
    ApplicationContainer senderApp = sender.Install(nodes.Get(0));
    senderApp.Start(Seconds(1.0));
    senderApp.Stop(Seconds(simulationTimeS - 0.1));

    std::cout << "\n=== Traffic timeline ===\n"
              << "[TRAFFIC] protocol=TCP application=BulkSend source_station=0 destination_station=1"
              << " destination=" << interfaces.GetAddress(1) << ":" << port
              << " offered_rate=saturated tcp_segment_bytes=" << packetSize << "\n"
              << "[APPLICATION-TIMELINE] sink=[0.5," << simulationTimeS
              << "] sender=[1.0," << (simulationTimeS - 0.1)
              << "] estimator=[0.0," << simulationTimeS << "] seconds\n"
              << "[ALGORITHM] for each local-destination TCP-DATA or reverse-flow "
                 "pure TCP-ACK received in a MAC-DATA MPDU: add "
                 "0.5*CW*ST+RTS+3*SIFS+CTS+TCP_FRAME+MAC_ACK+DIFS; each EP computes "
                 "TActive(k)=0.8*TActive(k-1)+0.2*t and resets t.\n";

    // Ownership must remain per station. unique_ptr keeps callback targets at
    // stable addresses and guarantees that no process-wide CATRA state is
    // accidentally shared between radios.
    std::vector<std::unique_ptr<CatraActiveTimeEstimator>> estimators;
    std::vector<uint32_t> initialCw;
    std::set<CatraFlowKey> dataFlows;
    for (uint32_t index = 0; index < nodes.GetN(); ++index)
    {
        auto estimator = std::make_unique<CatraActiveTimeEstimator>(
            index, Seconds(estimationPeriodS), 0.8, &PrintSample);
        Ptr<WifiNetDevice> localDevice = DynamicCast<WifiNetDevice>(devices.Get(index));
        Ptr<WifiNetDevice> peerDevice =
            DynamicCast<WifiNetDevice>(devices.Get(index == 0 ? 1 : 0));
        const bool receivedConnected = localDevice->GetMac()->TraceConnectWithoutContext(
            "MacRx",
            MakeBoundCallback(&ObserveLocalTcpPacket,
                              estimator.get(),
                              &dataFlows,
                              peerDevice,
                              localDevice,
                              interfaces.GetAddress(index)));
        NS_ABORT_MSG_IF(!receivedConnected,
                        "Failed to connect CATRA estimator to WifiMac/MacRx");
        estimator->Start(Seconds(0.0));
        estimator->Stop(Seconds(simulationTimeS));
        initialCw.push_back(localDevice->GetMac()->GetTxop()->GetCw(0));
        estimators.push_back(std::move(estimator));
        std::cout << "[CATRA-TRACE-CONNECT] station=" << index
                  << " trace=WifiMac/MacRx local_ipv4="
                  << interfaces.GetAddress(index)
                  << " status=connected\n";
    }

    // Let the report scheduled exactly at simulationTime execute before the
    // stop event; otherwise ns-3 event insertion order can hide the last EP.
    Simulator::Stop(Seconds(simulationTimeS) + NanoSeconds(1));
    Simulator::Run();

    Ptr<PacketSink> receiver = DynamicCast<PacketSink>(sinkApp.Get(0));
    const uint64_t receivedBytes = receiver->GetTotalRx();
    const bool receiverSawTcpData = estimators.at(1)->GetTotalTcpDataPackets() > 0;
    const bool sourceSawTcpAcks = estimators.at(0)->GetTotalTcpAckPackets() > 0;
    const bool sourceDidNotCountTcpData = estimators.at(0)->GetTotalTcpDataPackets() == 0;
    const bool receiverDidNotCountTcpAcks = estimators.at(1)->GetTotalTcpAckPackets() == 0;
    bool cwUnchanged = true;
    for (uint32_t index = 0; index < devices.GetN(); ++index)
    {
        Ptr<WifiNetDevice> device = DynamicCast<WifiNetDevice>(devices.Get(index));
        cwUnchanged = cwUnchanged && device->GetMac()->GetTxop()->GetCw(0) == initialCw.at(index);
    }
    const bool passed = receivedBytes > 0 && receiverSawTcpData && sourceSawTcpAcks &&
                        sourceDidNotCountTcpData && receiverDidNotCountTcpAcks && cwUnchanged;

    std::cout << "\n=== Active-time probe acceptance ===\n"
              << "tcp_sink_received_bytes=" << receivedBytes << "\n"
              << "receiver_received_tcp_data=" << (receiverSawTcpData ? "PASS" : "FAIL") << "\n"
              << "source_received_data_tcp_ack=" << (sourceSawTcpAcks ? "PASS" : "FAIL") << "\n"
              << "source_tcp_data_count_zero="
              << (sourceDidNotCountTcpData ? "PASS" : "FAIL") << "\n"
              << "receiver_handshake_ack_excluded="
              << (receiverDidNotCountTcpAcks ? "PASS" : "FAIL") << "\n"
              << "cw_read_only=" << (cwUnchanged ? "PASS" : "FAIL") << "\n"
              << "overall=" << (passed ? "PASS" : "FAIL") << "\n";

    Simulator::Destroy();
    return strict && !passed ? 1 : 0;
}
