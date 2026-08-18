/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// This executable validates the read-only core of CATRA Algorithm 1 against
// real ns-3 Wi-Fi PHY state transitions. It does not change CW and therefore
// cannot alter channel access behavior.

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
              << " raw_tx_s=" << sample.rawTxTime.GetSeconds()
              << " previous_weight=0.800000 current_weight=0.200000"
              << " smoothed_active_s=" << sample.smoothedActiveTime.GetSeconds()
              << " rbrs=" << sample.realBandwidthRatio
              << " tx_state_events=" << sample.txStateEvents << "\n";
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
    std::string offeredRate{"2Mbps"};
    bool strict{true};

    CommandLine cmd(__FILE__);
    cmd.AddValue("ep", "CATRA estimation period in seconds", estimationPeriodS);
    cmd.AddValue("simulationTime", "Probe stop time in seconds", simulationTimeS);
    cmd.AddValue("packetSize", "UDP payload size used to exercise the PHY", packetSize);
    cmd.AddValue("offeredRate", "Application offered rate", offeredRate);
    cmd.AddValue("strict", "Fail if no station records PHY TX activity", strict);
    cmd.Parse(argc, argv);

    NS_ABORT_MSG_IF(estimationPeriodS <= 0.0, "ep must be positive");
    NS_ABORT_MSG_IF(simulationTimeS <= estimationPeriodS, "simulationTime must exceed ep");

    std::cout << "\n=== CATRA Algorithm 1 active-time probe ===\n"
              << "mode=read-only cw_control=disabled packet_classifier=pending\n"
              << "measurement=exact-local-phy-tx-duration ep_s=" << estimationPeriodS
              << " smoothing=0.8*previous+0.2*current\n"
              << "[PORT-NOTE] The paper's full TCP transaction-time classification is not yet "
                 "claimed; this probe validates PHY TX accumulation and smoothing.\n";

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
    // PHY TX durations, allowing the probe to focus on active-time accounting.
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

    // Install one configured mobility model on every node in the container and
    // consume positions from GridPositionAllocator in NodeContainer order.
    mobility.Install(nodes);

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
                                 "ControlMode", StringValue("DsssRate11Mbps"));
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

    // Start the receiver before the sender. The sender runs long enough to
    // cover several complete EPs, making EWMA history visible in the logs.
    const uint16_t port = 9000;
    UdpServerHelper server(port);
    ApplicationContainer serverApp = server.Install(nodes.Get(1));
    serverApp.Start(Seconds(0.5));
    serverApp.Stop(Seconds(simulationTimeS));

    OnOffHelper sender("ns3::UdpSocketFactory", InetSocketAddress(interfaces.GetAddress(1), port));
    sender.SetAttribute("DataRate", DataRateValue(DataRate(offeredRate)));
    sender.SetAttribute("PacketSize", UintegerValue(packetSize));
    sender.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    sender.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    ApplicationContainer senderApp = sender.Install(nodes.Get(0));
    senderApp.Start(Seconds(1.0));
    senderApp.Stop(Seconds(simulationTimeS - 0.1));

    // Ownership must remain per station. unique_ptr keeps callback targets at
    // stable addresses and guarantees that no process-wide CATRA state is
    // accidentally shared between radios.
    std::vector<std::unique_ptr<CatraActiveTimeEstimator>> estimators;
    for (uint32_t index = 0; index < nodes.GetN(); ++index)
    {
        auto estimator = std::make_unique<CatraActiveTimeEstimator>(
            index, Seconds(estimationPeriodS), 0.8, &PrintSample);
        // WifiPhyStateHelper reports the exact start and duration of every PHY
        // state transition. Binding without context is safe because station
        // identity is already stored inside this estimator instance.
        Ptr<WifiNetDevice> wifiDevice = DynamicCast<WifiNetDevice>(devices.Get(index));
        const bool connected = wifiDevice->GetPhy()->GetState()->TraceConnectWithoutContext(
            "State", MakeCallback(&CatraActiveTimeEstimator::NotifyPhyState, estimator.get()));
        NS_ABORT_MSG_IF(!connected, "Failed to connect CATRA estimator to PHY State trace");
        estimator->Start(Seconds(0.0));
        estimator->Stop(Seconds(simulationTimeS));
        estimators.push_back(std::move(estimator));
        std::cout << "[CATRA-TRACE-CONNECT] station=" << index
                  << " trace=WifiPhyStateHelper/State status=connected\n";
    }

    // Let the report scheduled exactly at simulationTime execute before the
    // stop event; otherwise ns-3 event insertion order can hide the last EP.
    Simulator::Stop(Seconds(simulationTimeS) + NanoSeconds(1));
    Simulator::Run();

    // Acceptance checks observable behavior rather than merely proving that a
    // callback was connected: packets must arrive and both radios must report
    // non-zero smoothed TX time. The receiver's TX time comes from link-layer
    // control responses rather than UDP application data.
    Ptr<UdpServer> receiver = DynamicCast<UdpServer>(serverApp.Get(0));
    const uint64_t receivedPackets = receiver->GetReceived();
    const bool sourceActive = estimators.at(0)->GetSmoothedActiveTime() > Time(0);
    const bool receiverActive = estimators.at(1)->GetSmoothedActiveTime() > Time(0);
    const bool passed = receivedPackets > 0 && sourceActive && receiverActive;

    std::cout << "\n=== Active-time probe acceptance ===\n"
              << "received_packets=" << receivedPackets << "\n"
              << "source_tx_active=" << (sourceActive ? "PASS" : "FAIL") << "\n"
              << "receiver_control_tx_active=" << (receiverActive ? "PASS" : "FAIL") << "\n"
              << "cw_unchanged=PASS\n"
              << "overall=" << (passed ? "PASS" : "FAIL") << "\n";

    Simulator::Destroy();
    return strict && !passed ? 1 : 0;
}
