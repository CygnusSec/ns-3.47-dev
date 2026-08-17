/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Phase 2 of CATRA Scenario 1 builds the calibrated wireless chain only.
// It intentionally contains no traffic application, populated host route,
// TCP flow, throughput calculation, or CATRA controller.  Keeping this phase
// topology-only makes configuration errors visible before forwarding and
// transport behavior are introduced.

#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/propagation-module.h"
#include "ns3/simulation-debug-helper.h"
#include "ns3/wifi-module.h"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("CatraScenario1");

namespace
{

constexpr uint32_t MIN_STATIONS = 3;
constexpr uint32_t MAX_STATIONS = 6;
constexpr double CHANNEL_FREQUENCY_HZ = 2.412e9;
constexpr double POSITION_TOLERANCE_M = 1e-9;

/** Return the paper role associated with a chain index. */
std::string
GetNodeRole(uint32_t nodeIndex, uint32_t stationCount)
{
    if (nodeIndex == 0)
    {
        return "S2-long-flow-source";
    }
    if (nodeIndex == stationCount - 2)
    {
        return "S1-short-flow-source";
    }
    if (nodeIndex == stationCount - 1)
    {
        return "R-common-receiver";
    }
    return "relay";
}

/** Describe the calibrated relationship between two stations. */
std::string
GetCalibratedRelationship(double distanceM)
{
    if (distanceM <= 250.0)
    {
        return "decode";
    }
    if (distanceM <= 550.0)
    {
        return "cca-only";
    }
    return "none";
}

/** Print node ownership, position, device address, and IPv4 address. */
void
PrintNodeTable(const NodeContainer& nodes,
               const NetDeviceContainer& devices,
               const Ipv4InterfaceContainer& interfaces)
{
    std::cout << std::left << std::setw(8) << "index" << std::setw(7) << "node"
              << std::setw(24) << "role" << std::setw(14) << "position_x_m"
              << std::setw(22) << "mac" << "ipv4\n";

    for (uint32_t index = 0; index < nodes.GetN(); ++index)
    {
        const Vector position = nodes.Get(index)->GetObject<MobilityModel>()->GetPosition();
        const Mac48Address macAddress = Mac48Address::ConvertFrom(devices.Get(index)->GetAddress());
        std::ostringstream macText;
        macText << macAddress;
        std::cout << std::left << std::setw(8) << index << std::setw(7)
                  << ("n" + std::to_string(nodes.Get(index)->GetId())) << std::setw(24)
                  << GetNodeRole(index, nodes.GetN()) << std::setw(14) << std::fixed
                  << std::setprecision(1) << position.x << std::setw(22) << macText.str()
                  << interfaces.GetAddress(index)
                  << "/24\n";
    }
    std::cout << std::defaultfloat << std::setprecision(6);
}

/**
 * Print pairwise radio relationships established by the Phase 1 calibration.
 *
 * Wi-Fi devices share one YansWifiChannel, so this is not a graph of separate
 * link objects.  It documents which pairs can decode each other, which pairs
 * only contribute carrier sensing, and which pairs are below detection.
 */
void
PrintRadioRelationshipMatrix(const NodeContainer& nodes,
                             Ptr<TwoRayGroundPropagationLossModel> loss,
                             double txPowerDbm)
{
    std::cout << std::left << std::setw(10) << "pair" << std::setw(14) << "distance_m"
              << std::setw(16) << "rx_power_dbm" << "relationship\n";

    for (uint32_t left = 0; left < nodes.GetN(); ++left)
    {
        Ptr<MobilityModel> leftMobility = nodes.Get(left)->GetObject<MobilityModel>();
        for (uint32_t right = left + 1; right < nodes.GetN(); ++right)
        {
            const double distance =
                leftMobility->GetDistanceFrom(nodes.Get(right)->GetObject<MobilityModel>());
            const double rxPowerDbm = loss->CalcRxPower(
                txPowerDbm,
                leftMobility,
                nodes.Get(right)->GetObject<MobilityModel>());
            const std::string pair = "n" + std::to_string(left) + "-n" + std::to_string(right);
            std::cout << std::left << std::setw(10) << pair << std::setw(14) << distance
                      << std::setw(16) << std::fixed << std::setprecision(3) << rxPowerDbm
                      << GetCalibratedRelationship(distance) << "\n";
        }
    }
    std::cout << std::defaultfloat << std::setprecision(6);
}

/** Validate the structural invariants that belong to Phase 2. */
bool
ValidateTopology(const NodeContainer& nodes,
                 const NetDeviceContainer& devices,
                 const Ipv4InterfaceContainer& interfaces,
                 double spacingM)
{
    bool passed = true;

    auto check = [&passed](const std::string& name, bool condition) {
        std::cout << name << "=" << (condition ? "PASS" : "FAIL") << "\n";
        passed = passed && condition;
    };

    check("station_count", nodes.GetN() >= MIN_STATIONS && nodes.GetN() <= MAX_STATIONS);
    check("wifi_device_count", devices.GetN() == nodes.GetN());
    check("ipv4_interface_count", interfaces.GetN() == nodes.GetN());
    check("s2_index", GetNodeRole(0, nodes.GetN()) == "S2-long-flow-source");
    check("s1_index", GetNodeRole(nodes.GetN() - 2, nodes.GetN()) == "S1-short-flow-source");
    check("receiver_index", GetNodeRole(nodes.GetN() - 1, nodes.GetN()) == "R-common-receiver");

    bool positionsCorrect = true;
    bool adjacentSpacingCorrect = true;
    for (uint32_t index = 0; index < nodes.GetN(); ++index)
    {
        const Vector position = nodes.Get(index)->GetObject<MobilityModel>()->GetPosition();
        positionsCorrect = positionsCorrect &&
                           std::abs(position.x - index * spacingM) <= POSITION_TOLERANCE_M &&
                           std::abs(position.y) <= POSITION_TOLERANCE_M &&
                           std::abs(position.z) <= POSITION_TOLERANCE_M;

        if (index > 0)
        {
            const double distance = nodes.Get(index - 1)
                                        ->GetObject<MobilityModel>()
                                        ->GetDistanceFrom(nodes.Get(index)->GetObject<MobilityModel>());
            adjacentSpacingCorrect =
                adjacentSpacingCorrect && std::abs(distance - spacingM) <= POSITION_TOLERANCE_M;
        }
    }
    check("linear_positions", positionsCorrect);
    check("adjacent_spacing", adjacentSpacingCorrect);
    const double shortFlowDistance = nodes.Get(nodes.GetN() - 2)
                                         ->GetObject<MobilityModel>()
                                         ->GetDistanceFrom(nodes.Get(nodes.GetN() - 1)
                                                              ->GetObject<MobilityModel>());
    const double longFlowDistance = nodes.Get(0)->GetObject<MobilityModel>()->GetDistanceFrom(
        nodes.Get(nodes.GetN() - 1)->GetObject<MobilityModel>());
    check("short_flow_structural_hops",
          std::abs(shortFlowDistance / spacingM - 1.0) <= POSITION_TOLERANCE_M);
    check("long_flow_structural_hops",
          std::abs(longFlowDistance / spacingM - (nodes.GetN() - 1)) <= POSITION_TOLERANCE_M);
    check("overall", passed);
    return passed;
}

} // namespace

int
main(int argc, char* argv[])
{
    uint32_t stationCount{4};
    double spacingM{200.0};
    double txPowerDbm{16.0};
    double rxSensitivityDbm{-87.0};
    double ccaEdThresholdDbm{-87.0};
    double ccaSensitivityDbm{-75.0};
    double rxNoiseFigureDb{18.0};
    double antennaHeightM{1.5};
    double systemLoss{1.0};
    uint32_t seed{1};
    uint64_t run{1};
    bool printTopology{true};
    bool verboseDetails{true};
    bool enablePcap{false};
    bool strict{true};

    // PHY defaults mirror the values accepted by catra-phy-range-probe.  They
    // remain command-line options so a future recalibration can be evaluated
    // without editing the scenario source.
    CommandLine cmd(__FILE__);
    cmd.AddValue("n", "Number of stations in the Scenario 1 chain (3 through 6)", stationCount);
    cmd.AddValue("spacing", "Distance between adjacent stations in meters", spacingM);
    cmd.AddValue("txPower", "Fixed transmit power in dBm", txPowerDbm);
    cmd.AddValue("rxSensitivity", "PHY receive sensitivity in dBm", rxSensitivityDbm);
    cmd.AddValue("ccaEdThreshold", "PHY energy-detection threshold in dBm", ccaEdThresholdDbm);
    cmd.AddValue("ccaSensitivity", "Wi-Fi preamble CCA sensitivity in dBm", ccaSensitivityDbm);
    cmd.AddValue("rxNoiseFigure", "Receiver noise figure in dB", rxNoiseFigureDb);
    cmd.AddValue("antennaHeight", "Two-Ray antenna height above node Z in meters", antennaHeightM);
    cmd.AddValue("systemLoss", "Two-Ray dimensionless system loss", systemLoss);
    cmd.AddValue("seed", "Random-number seed", seed);
    cmd.AddValue("run", "Random-number run number", run);
    cmd.AddValue("printTopology", "Print the full discovered ns-3 topology", printTopology);
    cmd.AddValue("verboseDetails", "Print tagged per-node setup details", verboseDetails);
    cmd.AddValue("enablePcap", "Enable per-device PCAP files (no traffic exists in Phase 2)", enablePcap);
    cmd.AddValue("strict", "Return failure when a Phase 2 invariant is violated", strict);
    cmd.Parse(argc, argv);

    NS_ABORT_MSG_IF(stationCount < MIN_STATIONS || stationCount > MAX_STATIONS,
                    "Scenario 1 requires n in the range [3, 6]");
    NS_ABORT_MSG_IF(spacingM <= 0.0, "spacing must be positive");
    NS_ABORT_MSG_IF(systemLoss < 1.0, "systemLoss must be at least 1.0");

    RngSeedManager::SetSeed(seed);
    RngSeedManager::SetRun(run);

    std::cout << "\n=== 1. Scenario 1 Phase 2 configuration ===\n"
              << "mode=original-baseline phase=topology-only catra=disabled\n"
              << "stations=" << stationCount << " spacing_m=" << spacingM
              << " s2_index=0 s1_index=" << stationCount - 2
              << " receiver_index=" << stationCount - 1 << "\n"
              << "expected_short_flow_hops=1 expected_long_flow_hops=" << stationCount - 1
              << " seed=" << seed << " run=" << run
              << " verbose_details=" << std::boolalpha << verboseDetails << "\n"
              << "[PHASE-BOUNDARY] routes_populated=false udp=false tcp=false catra=false\n";

    // Set non-unicast mode even though Phase 2 sends no frames.  This keeps the
    // installed Wi-Fi configuration identical to the validated Phase 1 PHY and
    // prevents later broadcast validation traffic from falling back to 1 Mbps.
    Config::SetDefault("ns3::WifiRemoteStationManager::NonUnicastMode",
                       StringValue("DsssRate11Mbps"));
    Config::SetDefault("ns3::WifiRemoteStationManager::RtsCtsThreshold", UintegerValue(0));
    Config::SetDefault("ns3::WifiMacQueue::MaxSize", QueueSizeValue(QueueSize("100p")));

    std::cout << "\n=== 2. Create the linear station chain ===\n";
    NodeContainer nodes;
    nodes.Create(stationCount);

    // Index order is the forwarding order required by the paper: S2 is at the
    // left edge, zero or more relay nodes follow, then S1 and the common
    // receiver R occupy the final two adjacent positions.
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positions = CreateObject<ListPositionAllocator>();
    for (uint32_t index = 0; index < stationCount; ++index)
    {
        const Vector position(index * spacingM, 0.0, 0.0);
        positions->Add(position);
        if (verboseDetails)
        {
            std::cout << "[NODE-POSITION] index=" << index << " role="
                      << GetNodeRole(index, stationCount) << " x_m=" << position.x
                      << " y_m=" << position.y << " z_m=" << position.z << "\n";
        }
    }
    mobility.SetPositionAllocator(positions);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);
    std::cout << "created_nodes=" << nodes.GetN()
              << " mobility=ConstantPositionMobilityModel layout=linear\n";

    std::cout << "\n=== 3. Install the calibrated shared Wi-Fi channel ===\n";
    Ptr<TwoRayGroundPropagationLossModel> loss =
        CreateObject<TwoRayGroundPropagationLossModel>();
    loss->SetAttribute("Frequency", DoubleValue(CHANNEL_FREQUENCY_HZ));
    loss->SetAttribute("SystemLoss", DoubleValue(systemLoss));
    loss->SetAttribute("HeightAboveZ", DoubleValue(antennaHeightM));

    Ptr<YansWifiChannel> channel = CreateObject<YansWifiChannel>();
    channel->SetPropagationDelayModel(CreateObject<ConstantSpeedPropagationDelayModel>());
    channel->SetPropagationLossModel(loss);

    YansWifiPhyHelper phy;
    phy.SetChannel(channel);
    phy.Set("ChannelSettings", StringValue("{1, 22, BAND_2_4GHZ, 0}"));
    phy.Set("TxPowerStart", DoubleValue(txPowerDbm));
    phy.Set("TxPowerEnd", DoubleValue(txPowerDbm));
    phy.Set("RxSensitivity", DoubleValue(rxSensitivityDbm));
    phy.Set("CcaEdThreshold", DoubleValue(ccaEdThresholdDbm));
    phy.Set("CcaSensitivity", DoubleValue(ccaSensitivityDbm));
    phy.Set("RxNoiseFigure", DoubleValue(rxNoiseFigureDb));
    phy.Set("TxGain", DoubleValue(0.0));
    phy.Set("RxGain", DoubleValue(0.0));

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode",
                                 StringValue("DsssRate11Mbps"),
                                 "ControlMode",
                                 StringValue("DsssRate11Mbps"));

    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);
    wifi.AssignStreams(devices, 0);

    if (verboseDetails)
    {
        for (uint32_t index = 0; index < devices.GetN(); ++index)
        {
            const Mac48Address address = Mac48Address::ConvertFrom(devices.Get(index)->GetAddress());
            std::cout << "[WIFI-DEVICE] node=n" << nodes.Get(index)->GetId()
                      << " device_index=" << devices.Get(index)->GetIfIndex()
                      << " mac=" << address << " channel=shared-yans-wifi\n";
        }
    }

    std::cout << "standard=802.11b mode=DsssRate11Mbps mac=AdhocWifiMac"
              << " channel=1 frequency_hz=" << static_cast<uint64_t>(CHANNEL_FREQUENCY_HZ) << "\n"
              << std::defaultfloat << std::setprecision(6)
              << "tx_power_dbm=" << txPowerDbm << " rx_sensitivity_dbm=" << rxSensitivityDbm
              << " cca_ed_threshold_dbm=" << ccaEdThresholdDbm
              << " cca_sensitivity_dbm=" << ccaSensitivityDbm
              << " rx_noise_figure_db=" << rxNoiseFigureDb
              << " antenna_height_m=" << antennaHeightM << " system_loss=" << systemLoss << "\n"
              << "rts_cts_threshold_bytes=0 mac_queue_max_packets=100\n";

    std::cout << "\n=== 4. Install IPv4 without populated Scenario 1 routes ===\n";
    // Install only the static-routing implementation.  Phase 2 assigns
    // addresses but deliberately leaves the destination-specific host routes
    // empty; Phase 3 owns route installation and bidirectional UDP validation.
    Ipv4StaticRoutingHelper staticRouting;
    Ipv4ListRoutingHelper routingList;
    routingList.Add(staticRouting, 0);
    InternetStackHelper internet;
    internet.SetRoutingHelper(routingList);
    internet.Install(nodes);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);
    std::cout << "ipv4_network=10.1.1.0/24 routing=static-unpopulated"
              << " applications=0 traffic=none\n";
    if (verboseDetails)
    {
        for (uint32_t index = 0; index < interfaces.GetN(); ++index)
        {
            std::cout << "[IPV4-ASSIGN] node=n" << nodes.Get(index)->GetId()
                      << " address=" << interfaces.GetAddress(index)
                      << " mask=255.255.255.0 host_routes=0\n";
        }
    }

    if (enablePcap)
    {
        phy.EnablePcap("catra-scenario1-phase2", devices);
        std::cout << "pcap=enabled (files remain empty of data traffic in Phase 2)\n";
    }

    std::cout << "\n=== 5. Node and role mapping ===\n";
    PrintNodeTable(nodes, devices, interfaces);

    std::cout << "\n=== 6. Calibrated pairwise radio relationships ===\n";
    PrintRadioRelationshipMatrix(nodes, loss, txPowerDbm);

    if (printTopology)
    {
        // Attribute expansion is disabled to keep the discovered topology
        // readable while still showing devices, channel, mobility, IP, and
        // routing model ownership for every station.
        SimulationDebugHelper::PrintTopology("CATRA Scenario 1 Phase 2 Topology", false);
    }

    // PrintTopology is scheduled at simulation time zero.  Running the event
    // queue materializes that report, but no packet or application event is
    // scheduled in this topology-only phase.
    Simulator::Run();

    std::cout << "\n=== 7. Phase 2 structural acceptance ===\n";
    const bool topologyPassed = ValidateTopology(nodes, devices, interfaces, spacingM);
    std::cout << "next_phase=install-static-host-routes-and-validate-with-udp\n";

    Simulator::Destroy();
    return strict && !topologyPassed ? 1 : 0;
}
