/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// CATRA Scenario 1 is introduced through independently selectable phases.
// The topology mode preserves the calibrated Phase 2 behavior, while the
// route-probe mode adds deterministic routes and UDP validation traffic.
// baseline mode adds the paper's two saturated flows using the local Tahoe
// implementation. CATRA decisions are provided by the shared contrib module
// and can be applied as an adaptive DCF minimum contention window.

#include "baseline/scenario1-baseline.h"
#include "baseline/scenario1-mac-hop-measurement.h"
#include "baseline/tcp-tahoe.h"
#include "catra/scenario1-catra.h"
#include "catra/scenario1-contention.h"

#include "ns3/applications-module.h"
#include "ns3/catra-active-time-estimator.h"
#include "ns3/catra-mac-controller.h"
#include "ns3/catra-mac-transaction-tracker.h"
#include "ns3/observe-mac-frame.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/propagation-module.h"
#include "ns3/simulation-debug-helper.h"
#include "ns3/wifi-module.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("CatraScenario1");

namespace
{

constexpr uint32_t MIN_STATIONS = 3;
constexpr uint32_t MAX_STATIONS = 6;
constexpr double MAX_ADJACENT_DISTANCE_M = 250.0;
constexpr double CHANNEL_FREQUENCY_HZ = 2.412e9;
constexpr double POSITION_TOLERANCE_M = 1e-9;
constexpr uint16_t LONG_FORWARD_PORT = 7001;
constexpr uint16_t LONG_REVERSE_PORT = 7002;
constexpr uint16_t SHORT_FORWARD_PORT = 7003;
constexpr uint32_t ROUTE_PROBE_PACKETS = 5;

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

/** Resolve one distance for every adjacent pair in the chain. */
std::vector<double>
ParseAdjacentDistances(const std::string& distanceList,
                       uint32_t stationCount,
                       double fallbackSpacingM)
{
    if (distanceList.empty())
    {
        return std::vector<double>(stationCount - 1, fallbackSpacingM);
    }

    std::vector<double> distances;
    std::stringstream input(distanceList);
    std::string value;
    while (std::getline(input, value, ','))
    {
        NS_ABORT_MSG_IF(value.empty(), "distances contains an empty value");
        try
        {
            std::size_t parsedCharacters = 0;
            const double distance = std::stod(value, &parsedCharacters);
            NS_ABORT_MSG_IF(parsedCharacters != value.size(),
                            "Invalid distance value: " << value);
            distances.push_back(distance);
        }
        catch (const std::exception&)
        {
            NS_ABORT_MSG("Invalid distance value: " << value);
        }
    }
    NS_ABORT_MSG_IF(distances.size() != stationCount - 1,
                    "distances requires exactly n-1 values; n="
                        << stationCount << " requires " << stationCount - 1 << " values");
    return distances;
}

std::string
FormatAdjacentDistances(const std::vector<double>& distances)
{
    std::ostringstream output;
    for (uint32_t index = 0; index < distances.size(); ++index)
    {
        output << (index == 0 ? "" : "|") << distances.at(index);
    }
    return output.str();
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
                      << ToString(GetScenario1RadioRelationship(distance)) << "\n";
        }
    }
    std::cout << std::defaultfloat << std::setprecision(6);
}

/** Install deterministic host routes for the long flow and its reverse path. */
void
InstallScenarioRoutes(const NodeContainer& nodes,
                      const NetDeviceContainer& devices,
                      const Ipv4InterfaceContainer& interfaces,
                      const Ipv4StaticRoutingHelper& routingHelper,
                      bool verbose)
{
    const uint32_t receiverIndex = nodes.GetN() - 1;
    const Ipv4Address receiverAddress = interfaces.GetAddress(receiverIndex);
    const Ipv4Address s2Address = interfaces.GetAddress(0);

    for (uint32_t index = 0; index < receiverIndex; ++index)
    {
        Ptr<Ipv4> ipv4 = nodes.Get(index)->GetObject<Ipv4>();
        const int32_t outputInterface = ipv4->GetInterfaceForDevice(devices.Get(index));
        NS_ABORT_MSG_IF(outputInterface < 0, "No IPv4 interface for station " << index);
        Ptr<Ipv4StaticRouting> routing = routingHelper.GetStaticRouting(ipv4);
        routing->AddHostRouteTo(receiverAddress,
                                interfaces.GetAddress(index + 1),
                                static_cast<uint32_t>(outputInterface));
        if (verbose)
        {
            std::cout << "[STATIC-ROUTE] node=" << index << " destination=" << receiverAddress
                      << "/32 next_hop=" << interfaces.GetAddress(index + 1)
                      << " interface=" << outputInterface << " direction=forward\n";
        }
    }

    for (uint32_t index = 1; index <= receiverIndex; ++index)
    {
        Ptr<Ipv4> ipv4 = nodes.Get(index)->GetObject<Ipv4>();
        const int32_t outputInterface = ipv4->GetInterfaceForDevice(devices.Get(index));
        NS_ABORT_MSG_IF(outputInterface < 0, "No IPv4 interface for station " << index);
        Ptr<Ipv4StaticRouting> routing = routingHelper.GetStaticRouting(ipv4);
        routing->AddHostRouteTo(s2Address,
                                interfaces.GetAddress(index - 1),
                                static_cast<uint32_t>(outputInterface));
        if (verbose)
        {
            std::cout << "[STATIC-ROUTE] node=" << index << " destination=" << s2Address
                      << "/32 next_hop=" << interfaces.GetAddress(index - 1)
                      << " interface=" << outputInterface << " direction=reverse\n";
        }
    }
}

/** Install three small UDP flows that prove long-forward, long-reverse, and short paths. */
void
InstallRouteProbeApplications(const NodeContainer& nodes,
                              const Ipv4InterfaceContainer& interfaces)
{
    const uint32_t s1Index = nodes.GetN() - 2;
    const uint32_t receiverIndex = nodes.GetN() - 1;

    UdpServerHelper longForwardServer(LONG_FORWARD_PORT);
    UdpServerHelper longReverseServer(LONG_REVERSE_PORT);
    UdpServerHelper shortForwardServer(SHORT_FORWARD_PORT);
    ApplicationContainer servers;
    servers.Add(longForwardServer.Install(nodes.Get(receiverIndex)));
    servers.Add(longReverseServer.Install(nodes.Get(0)));
    servers.Add(shortForwardServer.Install(nodes.Get(receiverIndex)));
    servers.Start(Seconds(0.5));
    servers.Stop(Seconds(4.0));

    auto installClient = [&nodes](uint32_t sourceIndex,
                                  Ipv4Address destination,
                                  uint16_t port,
                                  Time start) {
        UdpClientHelper client(destination, port);
        client.SetAttribute("MaxPackets", UintegerValue(ROUTE_PROBE_PACKETS));
        client.SetAttribute("Interval", TimeValue(MilliSeconds(100)));
        client.SetAttribute("PacketSize", UintegerValue(100));
        ApplicationContainer application = client.Install(nodes.Get(sourceIndex));
        application.Start(start);
        application.Stop(Seconds(4.0));
    };

    installClient(0, interfaces.GetAddress(receiverIndex), LONG_FORWARD_PORT, Seconds(1.0));
    installClient(receiverIndex, interfaces.GetAddress(0), LONG_REVERSE_PORT, Seconds(1.7));
    installClient(s1Index,
                  interfaces.GetAddress(receiverIndex),
                  SHORT_FORWARD_PORT,
                  Seconds(2.4));
}

/** Validate route-probe delivery and IP hop counts from FlowMonitor. */
bool
ValidateRouteProbe(Ptr<FlowMonitor> monitor,
                   Ptr<Ipv4FlowClassifier> classifier,
                   uint32_t stationCount)
{
    struct ExpectedFlow
    {
        std::string name;
        uint16_t destinationPort;
        uint32_t expectedHops;
        bool found{false};
    };

    std::array<ExpectedFlow, 3> expected{{{"long_forward", LONG_FORWARD_PORT, stationCount - 1},
                                         {"long_reverse", LONG_REVERSE_PORT, stationCount - 1},
                                         {"short_forward", SHORT_FORWARD_PORT, 1}}};
    bool passed = true;
    monitor->CheckForLostPackets();

    for (const auto& [flowId, stats] : monitor->GetFlowStats())
    {
        const Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(flowId);
        if (tuple.protocol != 17)
        {
            continue;
        }
        for (auto& flow : expected)
        {
            if (tuple.destinationPort != flow.destinationPort)
            {
                continue;
            }
            flow.found = true;
            const double observedHops = stats.rxPackets == 0
                                            ? 0.0
                                            : 1.0 + 1.0 * stats.timesForwarded / stats.rxPackets;
            const bool deliveryPassed = stats.txPackets == ROUTE_PROBE_PACKETS &&
                                        stats.rxPackets == ROUTE_PROBE_PACKETS &&
                                        stats.lostPackets == 0;
            const bool hopsPassed =
                std::abs(observedHops - flow.expectedHops) <= POSITION_TOLERANCE_M;
            std::cout << "[ROUTE-PROBE] flow=" << flow.name << " src=" << tuple.sourceAddress
                      << " dst=" << tuple.destinationAddress << " tx=" << stats.txPackets
                      << " rx=" << stats.rxPackets << " forwarded=" << stats.timesForwarded
                      << " observed_hops=" << observedHops
                      << " expected_hops=" << flow.expectedHops
                      << " delivery=" << (deliveryPassed ? "PASS" : "FAIL")
                      << " path=" << (hopsPassed ? "PASS" : "FAIL") << "\n";
            passed = passed && deliveryPassed && hopsPassed;
        }
    }

    for (const auto& flow : expected)
    {
        if (!flow.found)
        {
            std::cout << "[ROUTE-PROBE] flow=" << flow.name << " result=FAIL reason=not-found\n";
            passed = false;
        }
    }
    std::cout << "route_probe_overall=" << (passed ? "PASS" : "FAIL") << "\n";
    return passed;
}

/** Validate the structural invariants that belong to Phase 2. */
bool
ValidateTopology(const NodeContainer& nodes,
                 const NetDeviceContainer& devices,
                 const Ipv4InterfaceContainer& interfaces,
                 const std::vector<double>& adjacentDistancesM)
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
    double expectedX = 0.0;
    for (uint32_t index = 0; index < nodes.GetN(); ++index)
    {
        const Vector position = nodes.Get(index)->GetObject<MobilityModel>()->GetPosition();
        positionsCorrect = positionsCorrect &&
                           std::abs(position.x - expectedX) <= POSITION_TOLERANCE_M &&
                           std::abs(position.y) <= POSITION_TOLERANCE_M &&
                           std::abs(position.z) <= POSITION_TOLERANCE_M;

        if (index > 0)
        {
            const double distance = nodes.Get(index - 1)
                                        ->GetObject<MobilityModel>()
                                        ->GetDistanceFrom(nodes.Get(index)->GetObject<MobilityModel>());
            adjacentSpacingCorrect = adjacentSpacingCorrect &&
                                     std::abs(distance - adjacentDistancesM.at(index - 1)) <=
                                         POSITION_TOLERANCE_M;
        }
        if (index < adjacentDistancesM.size())
        {
            expectedX += adjacentDistancesM.at(index);
        }
    }
    check("linear_positions", positionsCorrect);
    check("adjacent_distances", adjacentSpacingCorrect);
    const double shortFlowDistance = nodes.Get(nodes.GetN() - 2)
                                         ->GetObject<MobilityModel>()
                                         ->GetDistanceFrom(nodes.Get(nodes.GetN() - 1)
                                                              ->GetObject<MobilityModel>());
    const double longFlowDistance = nodes.Get(0)->GetObject<MobilityModel>()->GetDistanceFrom(
        nodes.Get(nodes.GetN() - 1)->GetObject<MobilityModel>());
    double expectedLongFlowDistance = 0.0;
    for (double distance : adjacentDistancesM)
    {
        expectedLongFlowDistance += distance;
    }
    check("short_flow_adjacent_distance",
          std::abs(shortFlowDistance - adjacentDistancesM.back()) <= POSITION_TOLERANCE_M);
    check("long_flow_chain_distance",
          std::abs(longFlowDistance - expectedLongFlowDistance) <= POSITION_TOLERANCE_M);
    check("overall", passed);
    return passed;
}

} // namespace

int
main(int argc, char* argv[])
{
    uint32_t stationCount{3};
    std::string mode{"topology"};
    double spacingM{200.0};
    std::string distanceList;
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
    std::string trafficProfile{"paper"};
    bool traceCw{false};
    bool verboseCw{false};
    bool measureMacHops{true};
    bool strict{true};
    double simulationTimeS{300.0};
    double trafficStartS{1.0};
    std::string csvPath{"results/catra/scenario1/tahoe-baseline.csv"};
    std::string stationCsvPath{"results/catra/scenario1/station-state.csv"};
    std::string cwTraceCsvPath{"results/catra/scenario1/cw-events.csv"};
    std::string macHopCsvPath{"results/catra/scenario1/mac-hop-timeseries.csv"};
    double estimationPeriodS{2.0};
    double macHopIntervalS{1.0};

    // PHY defaults mirror the values accepted by catra-phy-range-probe.  They
    // remain command-line options so a future recalibration can be evaluated
    // without editing the scenario source.
    CommandLine cmd(__FILE__);
    cmd.AddValue("mode", "Scenario phase: topology, route-probe, baseline, or measure-only", mode);
    cmd.AddValue("n", "Number of stations in the Scenario 1 chain (3 through 6)", stationCount);
    cmd.AddValue("spacing", "Legacy uniform distance between adjacent stations", spacingM);
    cmd.AddValue("distances",
                 "Comma-separated distance for each adjacent pair, for example 200,250",
                 distanceList);
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
    cmd.AddValue("enablePcap", "Enable per-device PCAP files", enablePcap);
    cmd.AddValue("trafficProfile",
                 "Traffic profile: paper or tcp-stress",
                 trafficProfile);
    cmd.AddValue("traceCw", "Write every CW and backoff event to CSV", traceCw);
    cmd.AddValue("verboseCw", "Print every CW and backoff event to stdout", verboseCw);
    cmd.AddValue("measureMacHops",
                 "Measure bidirectional per-hop MAC traffic including retries and control frames",
                 measureMacHops);
    cmd.AddValue("strict", "Return failure when a selected-phase invariant is violated", strict);
    cmd.AddValue("simTime", "Simulation stop time in seconds", simulationTimeS);
    cmd.AddValue("trafficStart", "TCP source start time in seconds", trafficStartS);
    cmd.AddValue("csv", "Baseline result CSV path", csvPath);
    cmd.AddValue("stationCsv", "CATRA station measurement CSV path", stationCsvPath);
    cmd.AddValue("cwTraceCsv", "MAC CW/backoff trace CSV path", cwTraceCsvPath);
    cmd.AddValue("macHopCsv", "Per-hop MAC time-series CSV path", macHopCsvPath);
    cmd.AddValue("ep", "CATRA estimation period in seconds", estimationPeriodS);
    cmd.AddValue("macHopInterval",
                 "Per-hop MAC measurement interval in seconds",
                 macHopIntervalS);
    cmd.Parse(argc, argv);

    NS_ABORT_MSG_IF(stationCount < MIN_STATIONS || stationCount > MAX_STATIONS,
                    "Scenario 1 requires n in the range [3, 6]");
    NS_ABORT_MSG_IF(mode != "topology" && mode != "route-probe" && mode != "baseline" &&
                        mode != "measure-only",
                    "mode must be topology, route-probe, baseline, or measure-only");
    NS_ABORT_MSG_IF(trafficProfile != "paper" && trafficProfile != "tcp-stress",
                    "trafficProfile must be paper or tcp-stress");
    NS_ABORT_MSG_IF(spacingM <= 0.0, "spacing must be positive");
    NS_ABORT_MSG_IF(systemLoss < 1.0, "systemLoss must be at least 1.0");
    NS_ABORT_MSG_IF(simulationTimeS <= trafficStartS,
                    "simTime must be greater than trafficStart");
    NS_ABORT_MSG_IF(estimationPeriodS <= 0.0, "ep must be positive");
    NS_ABORT_MSG_IF(macHopIntervalS <= 0.0, "macHopInterval must be positive");

    const std::vector<double> adjacentDistancesM =
        ParseAdjacentDistances(distanceList, stationCount, spacingM);
    for (uint32_t index = 0; index < adjacentDistancesM.size(); ++index)
    {
        NS_ABORT_MSG_IF(!std::isfinite(adjacentDistancesM.at(index)) ||
                            adjacentDistancesM.at(index) <= 0.0,
                        "Adjacent distance must be positive: n" << index << "-n" << index + 1);
        NS_ABORT_MSG_IF(adjacentDistancesM.at(index) > MAX_ADJACENT_DISTANCE_M,
                        "Adjacent distance exceeds the calibrated 250 m transmission range: n"
                            << index << "-n" << index + 1 << '=' << adjacentDistancesM.at(index)
                            << " m");
    }

    RngSeedManager::SetSeed(seed);
    RngSeedManager::SetRun(run);

    const bool routeProbeEnabled = mode == "route-probe";
    const bool baselineEnabled = mode == "baseline" || mode == "measure-only";
    const bool routesEnabled = routeProbeEnabled || baselineEnabled;
    // This executable exists only when the contributed ns-3 CATRA module is
    // enabled. Every real-TCP phase therefore applies Eq. (5) as adaptive CWmin;
    // there is no second runtime CATRA feature flag.
    const bool catraControlEnabled = baselineEnabled;
    // Algorithm 1 is the mandatory input to CATRA CW control. Both are active
    // together in every real-TCP phase of this module-backed executable.
    const bool measurementEnabled = baselineEnabled;
    NS_ABORT_MSG_IF(trafficProfile == "tcp-stress" && !baselineEnabled,
                    "trafficProfile=tcp-stress requires baseline or measure-only mode");
    const bool tcpStressEnabled = trafficProfile == "tcp-stress";
    const std::string adjacentDistancesTag = FormatAdjacentDistances(adjacentDistancesM);
    std::cout << "\n=== 1. Scenario 1 configuration ===\n"
              << "mode=" << mode
              << " phase=" << (routeProbeEnabled ? "static-route-validation"
                                 : catraControlEnabled ? "catra-adaptive-cw"
                                 : baselineEnabled     ? "paper-tcp-baseline"
                                                       : "topology-only")
              << " traffic_profile=" << trafficProfile
              << " channel_access_measurement=" << (measurementEnabled ? "on" : "off")
              << " catra_module=enabled"
              << " catra_control=" << (catraControlEnabled ? "on" : "off")
              << "\n"
              << "stations=" << stationCount << " adjacent_distances_m=";
    for (uint32_t index = 0; index < adjacentDistancesM.size(); ++index)
    {
        std::cout << (index == 0 ? "" : ",") << adjacentDistancesM.at(index);
    }
    std::cout
              << " s2_index=0 s1_index=" << stationCount - 2
              << " receiver_index=" << stationCount - 1 << "\n"
              << "expected_short_flow_hops=1 expected_long_flow_hops=" << stationCount - 1
              << " seed=" << seed << " run=" << run
              << " verbose_details=" << std::boolalpha << verboseDetails << "\n"
              << "[PHASE-BOUNDARY] routes_populated=" << routesEnabled
              << " udp=" << routeProbeEnabled << " tcp=" << baselineEnabled
              << " catra_measurement=" << measurementEnabled
              << " catra_control=" << catraControlEnabled
              << " catra_control_applied=" << (catraControlEnabled && measurementEnabled)
              << " tcp_stress_load=" << tcpStressEnabled
              << " cw_trace=" << traceCw
              << " mac_hop_measurement=" << (measureMacHops && baselineEnabled)
              << " mac_hop_interval_s=" << macHopIntervalS << "\n";

    // Set non-unicast mode even though Phase 2 sends no frames.  This keeps the
    // installed Wi-Fi configuration identical to the validated Phase 1 PHY and
    // prevents later broadcast validation traffic from falling back to 1 Mbps.
    Config::SetDefault("ns3::WifiRemoteStationManager::NonUnicastMode",
                       StringValue("DsssRate11Mbps"));
    Config::SetDefault("ns3::WifiRemoteStationManager::RtsCtsThreshold", UintegerValue(0));
    Config::SetDefault("ns3::WifiMacQueue::MaxSize", QueueSizeValue(QueueSize("100p")));
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(CatraTcpTahoe::GetTypeId()));
    Config::SetDefault("ns3::TcpL4Protocol::RecoveryType",
                       TypeIdValue(CatraTcpTahoeRecovery::GetTypeId()));
    Config::SetDefault("ns3::TcpSocket::SegmentSize",
                       UintegerValue(SCENARIO1_TCP_PAYLOAD_BYTES));
    Config::SetDefault("ns3::TcpSocketBase::Sack", BooleanValue(false));

    std::cout << "\n=== 2. Create the linear station chain ===\n";
    NodeContainer nodes;
    nodes.Create(stationCount);

    // Index order is the forwarding order required by the paper: S2 is at the
    // left edge, zero or more relay nodes follow, then S1 and the common
    // receiver R occupy the final two adjacent positions.
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positions = CreateObject<ListPositionAllocator>();
    double positionX = 0.0;
    for (uint32_t index = 0; index < stationCount; ++index)
    {
        const Vector position(positionX, 0.0, 0.0);
        positions->Add(position);
        if (verboseDetails)
        {
            std::cout << "[NODE-POSITION] index=" << index << " role="
                      << GetNodeRole(index, stationCount) << " x_m=" << position.x
                      << " y_m=" << position.y << " z_m=" << position.z << "\n";
        }
        if (index < adjacentDistancesM.size())
        {
            positionX += adjacentDistancesM.at(index);
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
    Ptr<Txop> referenceTxop = DynamicCast<WifiNetDevice>(devices.Get(0))->GetMac()->GetTxop();
    // Capture the standard 802.11 CW ceiling once. CATRA changes only CWmin;
    // CWmax remains the DCF/BEB upper bound used by Eq. (5).
    const uint32_t standardCwMinNs3 = referenceTxop->GetMinCw(0);
    const uint32_t standardCwMaxNs3 = referenceTxop->GetMaxCw(0);
    std::cout << "cw_representation=ns3-inclusive-upper-bound"
              << " resolved_cw_min=" << standardCwMinNs3
              << " resolved_cw_max=" << standardCwMaxNs3
              << " paper_window_min=" << standardCwMinNs3 + 1
              << " paper_window_max=" << standardCwMaxNs3 + 1 << "\n";

    std::unique_ptr<Scenario1CwTraceLogger> cwTraceLogger;
    if (traceCw)
    {
        cwTraceLogger = std::make_unique<Scenario1CwTraceLogger>(
            cwTraceCsvPath, stationCount, verboseCw);
        for (uint32_t index = 0; index < devices.GetN(); ++index)
        {
            Ptr<WifiNetDevice> device = DynamicCast<WifiNetDevice>(devices.Get(index));
            Ptr<Txop> txop = device->GetMac()->GetTxop();
            const Time slotTime = device->GetPhy()->GetSlot();
            cwTraceLogger->ObserveCw(index, txop, txop->GetCw(0), 0);
            const bool cwConnected = txop->TraceConnectWithoutContext(
                "CwTrace",
                MakeBoundCallback(&ObserveScenario1Cw, cwTraceLogger.get(), index, txop));
            const bool backoffConnected = txop->TraceConnectWithoutContext(
                "BackoffTrace",
                MakeBoundCallback(&ObserveScenario1Backoff,
                                  cwTraceLogger.get(),
                                  index,
                                  txop,
                                  slotTime));
            NS_ABORT_MSG_IF(!cwConnected || !backoffConnected,
                            "Failed to connect CW/backoff trace for node " << index);
        }
        std::cout << "[CW-TRACE-CONFIG] enabled=true csv=" << cwTraceCsvPath
                  << " verbose=" << verboseCw << "\n";
    }

    std::unique_ptr<Scenario1MacHopMeasurement> macHopMeasurement;
    if (baselineEnabled && measureMacHops)
    {
        macHopMeasurement = std::make_unique<Scenario1MacHopMeasurement>(
            stationCount,
            trafficStartS,
            simulationTimeS,
            macHopIntervalS,
            macHopCsvPath,
            trafficProfile,
            catraControlEnabled,
            seed,
            run,
            adjacentDistancesTag);
        macHopMeasurement->RegisterDevices(devices);
        for (uint32_t index = 0; index < devices.GetN(); ++index)
        {
            Ptr<WifiNetDevice> device = DynamicCast<WifiNetDevice>(devices.Get(index));
            const bool connected = device->GetPhy()->TraceConnectWithoutContext(
                "MonitorSnifferTx",
                MakeBoundCallback(&ObserveScenario1MacHopTx,
                                  macHopMeasurement.get(),
                                  index,
                                  device->GetPhy()));
            NS_ABORT_MSG_IF(!connected,
                            "Failed to connect per-hop MAC TX trace for node " << index);
        }
        std::cout << "[MAC-HOP-MEASUREMENT] enabled=true level=MAC directions=both"
                  << " includes=TCP-DATA,TCP-ACK,retries,RTS,CTS,MAC-ACK"
                  << " interval_s=" << macHopIntervalS << " csv=" << macHopCsvPath << "\n";
    }

    std::vector<std::unique_ptr<CatraActiveTimeEstimator>> estimators;
    std::vector<std::unique_ptr<CatraMacTransactionTracker>> trackers;
    std::map<Mac48Address, Ptr<WifiNetDevice>> devicesByAddress;
    bool measurementPassed = true;
    if (measurementEnabled)
    {
        const auto transmissions = BuildScenario1Transmissions(stationCount, tcpStressEnabled);
        const auto counts =
            CalculateScenario1StationFlowCounts(adjacentDistancesM, transmissions);
        const std::filesystem::path stationOutput(stationCsvPath);
        if (!stationOutput.parent_path().empty())
        {
            std::filesystem::create_directories(stationOutput.parent_path());
        }
        const bool writeHeader = !std::filesystem::exists(stationOutput) ||
                                 std::filesystem::file_size(stationOutput) == 0;
        const std::string stationHeader =
            "traffic_profile,catra_enabled,measurement_enabled,seed,run,n,"
            "adjacent_distances_m,sim_time_s,ep_s,time,node,role,nSEND,nTX,nCS,ntotal,FBRS,"
            "raw_active_s,smoothed_active_s,RBRS,packet_count,data_count,tcp_ack_count,"
            "average_cw,expected_backoff_s,rts_s,cts_s,tcp_frame_s,mac_ack_s,interframe_s,"
            "cw_before_ns3,cw_before_slots,cw_min_before_ns3,cw_max_before_ns3,"
            "RBRS_over_FBRS,cw_prime_raw_slots,cw_prime_slots,cw_prime_ns3,decision,applied,"
            "cw_after_ns3,cw_min_after_ns3,cw_max_after_ns3";
        if (writeHeader)
        {
            std::ofstream output(stationCsvPath, std::ios::app);
            output << stationHeader << '\n';
        }
        else
        {
            std::ifstream existing(stationCsvPath);
            std::string existingHeader;
            std::getline(existing, existingHeader);
            NS_ABORT_MSG_IF(existingHeader != stationHeader,
                            "Refusing to append to station CSV with incompatible schema: "
                                << stationCsvPath);
        }
        for (uint32_t index = 0; index < devices.GetN(); ++index)
        {
            Ptr<WifiNetDevice> device = DynamicCast<WifiNetDevice>(devices.Get(index));
            devicesByAddress.emplace(Mac48Address::ConvertFrom(device->GetAddress()), device);
        }
        const std::string measurementTag{"[CATRA-STATION]"};
        const std::string decisionMode{"module-applied"};
        for (uint32_t index = 0; index < devices.GetN(); ++index)
        {
            Ptr<WifiNetDevice> localDevice = DynamicCast<WifiNetDevice>(devices.Get(index));
            auto report =
                [&, index, counts, localDevice, measurementTag, decisionMode, catraControlEnabled,
                 standardCwMaxNs3](const CatraActiveTimeSample& sample) {
                const auto& flowCounts = counts.at(index);
                const uint64_t packetCount = sample.tcpDataPackets + sample.tcpAckPackets;
                const bool valid = std::isfinite(sample.realBandwidthRatio) &&
                                   sample.realBandwidthRatio >= 0.0 && flowCounts.nTotal > 0;
                Ptr<Txop> txop = localDevice->GetMac()->GetTxop();
                const uint32_t currentCwNs3 = txop->GetCw(0);
                const uint32_t currentCwSlots = currentCwNs3 + 1;
                const uint32_t minBefore = txop->GetMinCw(0);
                const uint32_t maxBefore = txop->GetMaxCw(0);
                const CatraMacDecision macDecision = CalculateCatraMacDecision(
                    currentCwNs3,
                    standardCwMaxNs3,
                    flowCounts.fairBandwidthRatio,
                    sample.realBandwidthRatio);
                measurementPassed = measurementPassed && valid;
                bool applied = false;

                // CW' becomes the adaptive DCF base. CWmax stays at the 802.11
                // value, so failures still invoke BEB and successes reset to CW'.
                if (catraControlEnabled && valid && flowCounts.fairBandwidthRatio > 0.0 &&
                    macDecision.action != CatraMacAction::NO_DATA_SEND_FLOW)
                {
                    if (cwTraceLogger)
                    {
                        cwTraceLogger->BeginCatraUpdate(index);
                    }
                    txop->SetMinCw(macDecision.ns3Cw, 0);
                    if (cwTraceLogger)
                    {
                        cwTraceLogger->EndCatraUpdate(index);
                    }
                    applied = true;
                    // This line proves CATRA control actually wrote the live MAC.
                    // In a real-TCP phase this line proves the enabled CATRA
                    // module wrote its decision to the live MAC.
                    std::cout << "[CATRA-CW-APPLIED] time_s=" << sample.periodEnd.GetSeconds()
                              << " node=" << index
                              << " cw_before_ns3=" << currentCwNs3
                              << " min_before=" << minBefore << " max_before=" << maxBefore
                              << " cw_after_ns3=" << txop->GetCw(0)
                              << " min_after=" << txop->GetMinCw(0)
                              << " max_after=" << txop->GetMaxCw(0)
                              << " target_ns3=" << macDecision.ns3Cw << "\n";
                }
                const uint32_t cwAfter = txop->GetCw(0);
                const uint32_t minAfter = txop->GetMinCw(0);
                const uint32_t maxAfter = txop->GetMaxCw(0);
                std::ofstream output(stationCsvPath, std::ios::app);
                output << trafficProfile << ',' << std::boolalpha << catraControlEnabled << ','
                       << measurementEnabled << ',' << seed << ',' << run << ',' << stationCount
                       << ',' << adjacentDistancesTag << ',' << std::fixed << std::setprecision(6)
                       << simulationTimeS << ',' << estimationPeriodS << ','
                       << sample.periodEnd.GetSeconds() << ','
                       << index << ',' << GetNodeRole(index, stationCount) << ','
                       << flowCounts.nSend << ',' << flowCounts.nTx << ','
                       << flowCounts.nCs << ',' << flowCounts.nTotal << ','
                       << flowCounts.fairBandwidthRatio << ',' << sample.rawActiveTime.GetSeconds()
                       << ',' << sample.smoothedActiveTime.GetSeconds() << ','
                       << sample.realBandwidthRatio << ',' << packetCount << ','
                       << sample.tcpDataPackets << ',' << sample.tcpAckPackets << ','
                       << sample.averageContentionWindow << ','
                       << sample.expectedBackoffTime.GetSeconds() << ','
                       << sample.rtsTime.GetSeconds() << ',' << sample.ctsTime.GetSeconds() << ','
                       << sample.tcpFrameTime.GetSeconds() << ','
                       << sample.macAckTime.GetSeconds() << ','
                       << sample.interframeTime.GetSeconds() << ',' << currentCwNs3 << ','
                       << currentCwSlots << ',' << minBefore << ',' << maxBefore << ','
                       << macDecision.ratio << ','
                       << macDecision.rawWindowSlots << ',' << macDecision.windowSlots << ','
                       << macDecision.ns3Cw << ',' << ToString(macDecision.action) << ',' << applied
                       << ',' << cwAfter << ',' << minAfter << ',' << maxAfter << '\n';
                std::cout << measurementTag << " time_s=" << sample.periodEnd.GetSeconds()
                          << " node=" << index << " role=" << GetNodeRole(index, stationCount)
                          << " nSEND=" << flowCounts.nSend
                          << " nTX=" << flowCounts.nTx << " nCS=" << flowCounts.nCs
                          << " ntotal=" << flowCounts.nTotal
                          << " FBRS=" << flowCounts.fairBandwidthRatio
                          << " RBRS=" << sample.realBandwidthRatio
                          << " raw_active_s=" << sample.rawActiveTime.GetSeconds()
                          << " smoothed_active_s=" << sample.smoothedActiveTime.GetSeconds()
                          << " packets=" << packetCount
                          << " data=" << sample.tcpDataPackets
                          << " tcp_ack=" << sample.tcpAckPackets
                          << " average_cw=" << sample.averageContentionWindow
                          << " current_cw_ns3=" << currentCwNs3
                          << " current_cw_slots=" << currentCwSlots
                          << " ratio_RBRS_FBRS=" << macDecision.ratio
                          << " cw_prime_raw_slots=" << macDecision.rawWindowSlots
                          << " cw_prime_slots=" << macDecision.windowSlots
                          << " cw_prime_ns3=" << macDecision.ns3Cw
                          << " decision=" << ToString(macDecision.action)
                          << " decision_mode=" << decisionMode
                          << " applied=" << applied << " cw_after_ns3=" << cwAfter
                          << " min_after=" << minAfter << " max_after=" << maxAfter
                          << " validation=" << (valid ? "PASS" : "FAIL") << "\n";
            };
            auto estimator = std::make_unique<CatraActiveTimeEstimator>(
                index, Seconds(estimationPeriodS), 0.8, report);
            auto tracker = std::make_unique<CatraMacTransactionTracker>();
            const bool rxConnected = localDevice->GetPhy()->TraceConnectWithoutContext(
                "MonitorSnifferRx",
                MakeBoundCallback(&ObserveMacFrameRx,
                                  estimator.get(),
                                  tracker.get(),
                                  &devicesByAddress,
                                  localDevice));
            const bool txConnected = localDevice->GetPhy()->TraceConnectWithoutContext(
                "MonitorSnifferTx",
                MakeBoundCallback(&ObserveMacFrameTx, estimator.get(), tracker.get()));
            NS_ABORT_MSG_IF(!rxConnected || !txConnected,
                            "Failed to connect Scenario 1 CATRA measurement traces");
            estimator->Start(Seconds(trafficStartS));
            estimator->Stop(Seconds(simulationTimeS));
            estimators.push_back(std::move(estimator));
            trackers.push_back(std::move(tracker));
        }
    }

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

    std::cout << "\n=== 4. Install IPv4 and selected routing phase ===\n";
    // Every mode installs only static routing. The topology mode leaves its
    // tables empty; route-probe adds explicit /32 next-hop routes.
    Ipv4StaticRoutingHelper staticRouting;
    Ipv4ListRoutingHelper routingList;
    routingList.Add(staticRouting, 0);
    InternetStackHelper internet;
    internet.SetRoutingHelper(routingList);
    internet.Install(nodes);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);
    if (routesEnabled)
    {
        InstallScenarioRoutes(nodes, devices, interfaces, staticRouting, verboseDetails);
    }
    std::cout << "ipv4_network=10.1.1.0/24 routing="
              << (routesEnabled ? "static-host-routes" : "static-unpopulated")
              << " applications="
              << (routeProbeEnabled ? 6 : baselineEnabled ? (tcpStressEnabled ? 6 : 4) : 0)
              << " traffic="
              << (routeProbeEnabled ? "udp-route-probes"
                                    : tcpStressEnabled ? "three-saturated-tcp-flows"
                                    : baselineEnabled  ? "two-saturated-tcp-flows"
                                                       : "none")
              << "\n";
    if (verboseDetails)
    {
        for (uint32_t index = 0; index < interfaces.GetN(); ++index)
        {
            std::cout << "[IPV4-ASSIGN] node=n" << nodes.Get(index)->GetId()
                      << " address=" << interfaces.GetAddress(index)
                      << " mask=255.255.255.0 host_routes="
                      << (routesEnabled ? "phase-specific" : "0") << "\n";
        }
    }

    FlowMonitorHelper flowMonitorHelper;
    Ptr<FlowMonitor> flowMonitor;
    Ptr<Ipv4FlowClassifier> flowClassifier;
    std::vector<uint64_t> flow2ForwardedByNode(stationCount, 0);
    // Per-node received TCP-DATA bytes for each flow, used to derive per-hop
    // throughput (bytes received at node j == throughput of hop j-1 -> j).
    std::vector<uint64_t> flow1RxBytesByNode(stationCount, 0);
    std::vector<uint64_t> flow2RxBytesByNode(stationCount, 0);
    Scenario1BaselineApplications baselineApplications;
    if (routeProbeEnabled)
    {
        std::cout << "\n=== 5. Install bidirectional UDP route probes ===\n";
        InstallRouteProbeApplications(nodes, interfaces);
        flowMonitor = flowMonitorHelper.InstallAll();
        flowClassifier = DynamicCast<Ipv4FlowClassifier>(flowMonitorHelper.GetClassifier());
        NS_ABORT_MSG_IF(!flowClassifier, "Expected an IPv4 FlowMonitor classifier");
        Simulator::Stop(Seconds(4.0));
    }
    else if (baselineEnabled)
    {
        std::cout << "\n=== 5. Install Scenario 1 TCP baseline ===\n"
                  << "tcp=CatraTcpTahoe implementation=CatraTcpTahoe+TahoeRecovery sack=false"
                  << " classification=PORT payload_bytes=" << SCENARIO1_TCP_PAYLOAD_BYTES
                  << " traffic_start_s=" << trafficStartS
                  << " simulation_stop_s=" << simulationTimeS << "\n";
        baselineApplications = InstallScenario1Baseline(
            nodes, interfaces, trafficStartS, simulationTimeS);
        if (tcpStressEnabled)
        {
            baselineApplications.tcpStressSink = InstallScenario1TcpStressTraffic(
                nodes, interfaces, trafficStartS, simulationTimeS);
        }
        else
        {
            std::cout << "[CONTENTION-LOAD] enabled=false"
                      << " profile=paper protocol=none"
                      << " note=Flow1_and_Flow2_still_use_DCF\n";
        }
        flowMonitor = flowMonitorHelper.InstallAll();
        flowClassifier = DynamicCast<Ipv4FlowClassifier>(flowMonitorHelper.GetClassifier());
        for (uint32_t index = 0; index < stationCount; ++index)
        {
            Ptr<Ipv4L3Protocol> ipv4 = nodes.Get(index)->GetObject<Ipv4L3Protocol>();
            const bool connected = ipv4->TraceConnectWithoutContext(
                "UnicastForward",
                MakeBoundCallback(&ObserveScenario1Flow2Forward, &flow2ForwardedByNode, index));
            NS_ABORT_MSG_IF(!connected, "Failed to connect IPv4 UnicastForward trace");
            // "Rx" fires for every IP packet delivered up at this node, including
            // packets that will be forwarded. Counting TCP-DATA bytes per node
            // gives the throughput that crossed the hop into this node.
            const bool rxConnected = ipv4->TraceConnectWithoutContext(
                "Rx",
                MakeBoundCallback(&ObserveScenario1HopRx,
                                  &flow1RxBytesByNode,
                                  &flow2RxBytesByNode,
                                  index));
            NS_ABORT_MSG_IF(!rxConnected, "Failed to connect IPv4 Rx trace");
        }
        Simulator::Stop(Seconds(simulationTimeS));
    }

    if (enablePcap)
    {
        phy.EnablePcap("catra-scenario1", devices);
        std::cout << "pcap=enabled mode=" << mode << "\n";
    }

    std::cout << "\n=== 6. Node and role mapping ===\n";
    PrintNodeTable(nodes, devices, interfaces);

    std::cout << "\n=== 7. Calibrated pairwise radio relationships ===\n";
    PrintRadioRelationshipMatrix(nodes, loss, txPowerDbm);

    if (printTopology)
    {
        // Attribute expansion is disabled to keep the discovered topology
        // readable while still showing devices, channel, mobility, IP, and
        // routing model ownership for every station.
        SimulationDebugHelper::PrintTopology("CATRA Scenario 1 Topology", false);
    }

    // PrintTopology is scheduled at simulation time zero. Running the event
    // queue materializes that report and, in route-probe mode, the UDP probes.
    Simulator::Run();

    if (macHopMeasurement)
    {
        macHopMeasurement->WriteCsv();
        std::cout << "mac_hop_timeseries_csv=" << macHopCsvPath << "\n";
    }

    if (cwTraceLogger)
    {
        cwTraceLogger->PrintSummary();
    }

    std::cout << "\n=== 8. Structural acceptance ===\n";
    const bool topologyPassed =
        ValidateTopology(nodes, devices, interfaces, adjacentDistancesM);
    const bool routeProbePassed =
        !routeProbeEnabled || ValidateRouteProbe(flowMonitor, flowClassifier, stationCount);
    const bool baselinePassed =
        !baselineEnabled ||
        (WriteScenario1BaselineMetrics(baselineApplications,
                                       stationCount,
                                       seed,
                                       run,
                                       trafficProfile,
                                       catraControlEnabled,
                                       measurementEnabled,
                                       adjacentDistancesTag,
                                       trafficStartS,
                                       simulationTimeS,
                                       estimationPeriodS,
                                       csvPath,
                                       flow2ForwardedByNode,
                                       flow1RxBytesByNode,
                                       flow2RxBytesByNode) &&
         ValidateScenario1Baseline(
             flowMonitor,
             flowClassifier,
             stationCount,
             tcpStressEnabled,
             flow2ForwardedByNode));
    if (measurementEnabled)
    {
        bool observed = false;
        for (const auto& estimator : estimators)
        {
            observed = observed || estimator->GetTotalTcpDataPackets() > 0 ||
                       estimator->GetTotalTcpAckPackets() > 0;
        }
        measurementPassed = measurementPassed && observed;
        std::cout << "catra_measurement_overall="
                  << (measurementPassed ? "PASS" : "FAIL") << "\n";
    }
    const bool overallPassed =
        topologyPassed && routeProbePassed && baselinePassed && measurementPassed;
    std::cout << "scenario_overall="
              << (overallPassed ? "PASS" : "FAIL") << "\n"
              << "completed_phase="
              << (routeProbeEnabled ? "static-route-validation"
                  : catraControlEnabled ? "catra-adaptive-cw"
                  : measurementEnabled  ? "algorithm1-measurement"
                  : baselineEnabled     ? "paper-tcp-baseline"
                                        : "topology")
              << " traffic_profile=" << trafficProfile << "\n";

    Simulator::Destroy();
    return strict && !overallPassed ? 1 : 0;
}
