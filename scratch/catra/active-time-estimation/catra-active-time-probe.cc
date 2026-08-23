/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// This executable validates CATRA Algorithm 1 from complete 802.11 frames.
// MonitorSnifferRx checks p.destID == localID and classifies the TCP payload;
// MonitorSnifferTx confirms that the local station actually transmits the MAC
// ACK paired with a pending TCP-DATA frame. CW remains read-only.

#include "catra-active-time-estimator.h"
#include "catra-mac-transaction-tracker.h"
#include "observe-mac-frame.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include <iomanip>
#include <iostream>
#include <memory>
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
                 "packet_classifier=correlated-mac-ack-tcp-data-or-mac-data-tcp-ack\n"
              << "measurement=modeled-complete-wifi-transaction-time ep_s=" << estimationPeriodS
              << " smoothing=0.8*previous+0.2*current\n"
              << "[ALGORITHM-MAPPING] MonitorSnifferRx checks MAC destination=local and "
                 "parses TCP; MonitorSnifferTx must observe the paired local MAC ACK before "
                 "TCP-DATA is counted; MAC-DATA plus pure TCP ACK maps TCPACK.\n";

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
              << "[ALGORITHM] count local-destination TCP-DATA only after its paired local "
                 "MAC ACK transmission; count local-destination MAC-DATA carrying "
                 "pure TCP-ACK; add "
                 "0.5*CW*ST+RTS+3*SIFS+CTS+TCP_FRAME+MAC_ACK+DIFS; each EP computes "
                 "TActive(k)=0.8*TActive(k-1)+0.2*t and resets t.\n";

    // Ownership must remain per station. unique_ptr keeps callback targets at
    // stable addresses and guarantees that no process-wide CATRA state is
    // accidentally shared between radios.
    std::vector<std::unique_ptr<CatraActiveTimeEstimator>> estimators;
    std::vector<std::unique_ptr<CatraMacTransactionTracker>> trackers;
    std::vector<uint32_t> initialCw;
    for (uint32_t index = 0; index < nodes.GetN(); ++index)
    {
        auto estimator = std::make_unique<CatraActiveTimeEstimator>(
            index, Seconds(estimationPeriodS), 0.8, &PrintSample);
        auto tracker = std::make_unique<CatraMacTransactionTracker>();
        Ptr<WifiNetDevice> localDevice = DynamicCast<WifiNetDevice>(devices.Get(index));
        Ptr<WifiNetDevice> peerDevice =
            DynamicCast<WifiNetDevice>(devices.Get(index == 0 ? 1 : 0));
        const bool receivedConnected = localDevice->GetPhy()->TraceConnectWithoutContext(
            "MonitorSnifferRx",
            MakeBoundCallback(&ObserveMacFrameRx,
                              estimator.get(),
                              tracker.get(),
                              peerDevice,
                              localDevice));
        const bool transmittedConnected = localDevice->GetPhy()->TraceConnectWithoutContext(
            "MonitorSnifferTx",
            MakeBoundCallback(&ObserveMacFrameTx, estimator.get(), tracker.get()));
        NS_ABORT_MSG_IF(!receivedConnected,
                        "Failed to connect CATRA estimator to WifiPhy/MonitorSnifferRx");
        NS_ABORT_MSG_IF(!transmittedConnected,
                        "Failed to connect CATRA estimator to WifiPhy/MonitorSnifferTx");
        estimator->Start(Seconds(0.0));
        estimator->Stop(Seconds(simulationTimeS));
        initialCw.push_back(localDevice->GetMac()->GetTxop()->GetCw(0));
        estimators.push_back(std::move(estimator));
        trackers.push_back(std::move(tracker));
        std::cout << "[CATRA-TRACE-CONNECT] station=" << index
                  << " traces=WifiPhy/MonitorSnifferRx+WifiPhy/MonitorSnifferTx local_ipv4="
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
    const bool receiverMacAckCorrelationComplete =
        trackers.at(1)->GetMacAcksMatched() > 0 &&
        trackers.at(1)->GetMacAcksMatched() == estimators.at(1)->GetTotalTcpDataPackets() &&
        trackers.at(1)->GetPendingCount() == 0;
    bool cwUnchanged = true;
    for (uint32_t index = 0; index < devices.GetN(); ++index)
    {
        Ptr<WifiNetDevice> device = DynamicCast<WifiNetDevice>(devices.Get(index));
        cwUnchanged = cwUnchanged && device->GetMac()->GetTxop()->GetCw(0) == initialCw.at(index);
    }
    const bool passed = receivedBytes > 0 && receiverSawTcpData && sourceSawTcpAcks &&
                        receiverMacAckCorrelationComplete && cwUnchanged;

    std::cout << "\n=== Active-time probe acceptance ===\n"
              << "tcp_sink_received_bytes=" << receivedBytes << "\n"
              << "receiver_received_tcp_data=" << (receiverSawTcpData ? "PASS" : "FAIL") << "\n"
              << "source_received_data_tcp_ack=" << (sourceSawTcpAcks ? "PASS" : "FAIL") << "\n"
              << "receiver_tcp_data_frames_observed="
              << trackers.at(1)->GetTcpDataFramesObserved() << "\n"
              << "receiver_mac_acks_matched=" << trackers.at(1)->GetMacAcksMatched() << "\n"
              << "receiver_pending_tcp_data=" << trackers.at(1)->GetPendingCount() << "\n"
              << "receiver_mac_ack_correlation="
              << (receiverMacAckCorrelationComplete ? "PASS" : "FAIL") << "\n"
              << "cw_read_only=" << (cwUnchanged ? "PASS" : "FAIL") << "\n"
              << "overall=" << (passed ? "PASS" : "FAIL") << "\n";

    Simulator::Destroy();
    return strict && !passed ? 1 : 0;
}
