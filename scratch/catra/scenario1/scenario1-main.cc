/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// CATRA Scenario 1 is introduced through independently selectable phases.
// The topology mode preserves the calibrated Phase 2 behavior, while the
// route-probe mode adds deterministic routes and UDP validation traffic.
// baseline mode adds the paper's two saturated flows using the local Tahoe
// implementation. CATRA decisions are provided by the shared contrib module;
// this executable currently integrates them in read-only measurement mode.

#include "baseline/scenario1-baseline.h"
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
    // Explicit CATRA control switch. Empty means "derive from mode" so existing
    // command lines keep their behavior. "on" enables CATRA control intent;
    // "off" runs the plain baseline. Note: this switch is about applying CATRA
    // control, not about whether MAC channel access is measured (see --measure).
    std::string catraSwitch;
    // Independent switch for the passive Algorithm 1 channel-access measurement.
    // Empty = measure whenever real TCP traffic exists (baseline or CATRA), so
    // the per-station channel access is visible even without CATRA. "on"/"off"
    // force it explicitly.
    std::string measureSwitch;
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
    bool enableContention{false};
    bool traceCw{false};
    bool verboseCw{false};
    bool strict{true};
    double simulationTimeS{300.0};
    double trafficStartS{1.0};
    std::string csvPath{"results/catra/scenario1/tahoe-baseline.csv"};
    std::string stationCsvPath{"results/catra/scenario1/station-state.csv"};
    std::string cwTraceCsvPath{"results/catra/scenario1/cw-events.csv"};
    double estimationPeriodS{2.0};

    // PHY defaults mirror the values accepted by catra-phy-range-probe.  They
    // remain command-line options so a future recalibration can be evaluated
    // without editing the scenario source.
    CommandLine cmd(__FILE__);
    cmd.AddValue("mode", "Scenario phase: topology, route-probe, baseline, or measure-only", mode);
    cmd.AddValue("catra",
                 "CATRA control switch: on, off, or empty to follow mode",
                 catraSwitch);
    cmd.AddValue("measure",
                 "MAC channel-access measurement (Algorithm 1): on, off, or empty to measure "
                 "whenever TCP traffic exists",
                 measureSwitch);
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
    cmd.AddValue("enableContention",
                 "Add a saturated UDP MAC contender at the receiver",
                 enableContention);
    cmd.AddValue("traceCw", "Write every CW and backoff event to CSV", traceCw);
    cmd.AddValue("verboseCw", "Print every CW and backoff event to stdout", verboseCw);
    cmd.AddValue("strict", "Return failure when a selected-phase invariant is violated", strict);
    cmd.AddValue("simTime", "Simulation stop time in seconds", simulationTimeS);
    cmd.AddValue("trafficStart", "TCP source start time in seconds", trafficStartS);
    cmd.AddValue("csv", "Baseline result CSV path", csvPath);
    cmd.AddValue("stationCsv", "CATRA station measurement CSV path", stationCsvPath);
    cmd.AddValue("cwTraceCsv", "MAC CW/backoff trace CSV path", cwTraceCsvPath);
    cmd.AddValue("ep", "CATRA estimation period in seconds", estimationPeriodS);
    cmd.Parse(argc, argv);

    NS_ABORT_MSG_IF(stationCount < MIN_STATIONS || stationCount > MAX_STATIONS,
                    "Scenario 1 requires n in the range [3, 6]");
    NS_ABORT_MSG_IF(mode != "topology" && mode != "route-probe" && mode != "baseline" &&
                        mode != "measure-only",
                    "mode must be topology, route-probe, baseline, or measure-only");
    NS_ABORT_MSG_IF(catraSwitch != "" && catraSwitch != "on" && catraSwitch != "off",
                    "catra must be on, off, or empty");
    NS_ABORT_MSG_IF(measureSwitch != "" && measureSwitch != "on" && measureSwitch != "off",
                    "measure must be on, off, or empty");
    // The CATRA switch only makes sense once real TCP traffic exists. Reject it
    // in the topology-only and route-probe phases so the intent stays clear.
    NS_ABORT_MSG_IF(catraSwitch != "" && mode != "baseline" && mode != "measure-only",
                    "catra=on/off requires mode=baseline or mode=measure-only");
    NS_ABORT_MSG_IF(spacingM <= 0.0, "spacing must be positive");
    NS_ABORT_MSG_IF(systemLoss < 1.0, "systemLoss must be at least 1.0");
    NS_ABORT_MSG_IF(simulationTimeS <= trafficStartS,
                    "simTime must be greater than trafficStart");
    NS_ABORT_MSG_IF(estimationPeriodS <= 0.0, "ep must be positive");

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
    // CATRA *control* intent. --catra=on/off overrides the mode default. Control
    // is not yet wired (catra_control stays false below), but the switch records
    // the user's intent and is reported for clarity.
    const bool catraControlEnabled = catraSwitch == "on"    ? true
                                     : catraSwitch == "off" ? false
                                                            : mode == "measure-only";
    // Passive Algorithm 1 channel-access measurement. Decoupled from the CATRA
    // control switch so per-station channel access is observed even without
    // CATRA. Default: measure whenever real TCP traffic exists. --measure=off
    // suppresses it; --measure=on forces it (still requires TCP traffic).
    const bool measurementEnabled = measureSwitch == "on"    ? true
                                    : measureSwitch == "off" ? false
                                                             : baselineEnabled;
    NS_ABORT_MSG_IF(measurementEnabled && !baselineEnabled,
                    "measure=on requires mode=baseline or mode=measure-only");
    // CATRA control applies CW' inside the per-EP measurement callback, so it
    // cannot run without measurement. Reject the contradictory combination
    // rather than silently ignoring the control switch.
    NS_ABORT_MSG_IF(catraControlEnabled && !measurementEnabled,
                    "catra=on requires measurement (do not combine with measure=off)");
    NS_ABORT_MSG_IF(enableContention && !baselineEnabled,
                    "enableContention requires baseline or measure-only mode");
    std::cout << "\n=== 1. Scenario 1 configuration ===\n"
              << "mode=" << mode
              << " phase=" << (routeProbeEnabled ? "static-route-validation"
                                 : baselineEnabled ? "original-tcp-baseline"
                                                   : "topology-only")
              << " channel_access_measurement=" << (measurementEnabled ? "on" : "off")
              << " catra_control=" << (catraControlEnabled ? "on" : "off")
              << " catra_switch=" << (catraSwitch.empty() ? "follow-mode" : catraSwitch)
              << " measure_switch=" << (measureSwitch.empty() ? "auto" : measureSwitch) << "\n"
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
              << " extra_contention_load=" << enableContention
              << " cw_trace=" << traceCw << "\n";

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
    // Capture the standard 802.11 CW bounds once. CATRA control pins a station's
    // live cwMin/cwMax to CW', so GetMaxCw() would no longer report the true
    // ceiling on later periods. Eq. (5) must always clamp against the original
    // CWmax, so we keep these fixed references for the decision arithmetic.
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
            cwTraceLogger->ObserveCw(index, txop->GetCw(0), 0);
            const bool cwConnected = txop->TraceConnectWithoutContext(
                "CwTrace",
                MakeBoundCallback(&ObserveScenario1Cw, cwTraceLogger.get(), index));
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

    std::vector<std::unique_ptr<CatraActiveTimeEstimator>> estimators;
    std::vector<std::unique_ptr<CatraMacTransactionTracker>> trackers;
    std::map<Mac48Address, Ptr<WifiNetDevice>> devicesByAddress;
    bool measurementPassed = true;
    if (measurementEnabled)
    {
        const auto counts = CalculateScenario1StationFlowCounts(stationCount);
        const std::filesystem::path stationOutput(stationCsvPath);
        if (!stationOutput.parent_path().empty())
        {
            std::filesystem::create_directories(stationOutput.parent_path());
        }
        const bool writeHeader = !std::filesystem::exists(stationOutput) ||
                                 std::filesystem::file_size(stationOutput) == 0;
        if (writeHeader)
        {
            std::ofstream output(stationCsvPath, std::ios::app);
            output << "time,node,role,nSEND,nTX,nCS,ntotal,FBRS,raw_active_s,"
                      "smoothed_active_s,RBRS,packet_count,data_count,tcp_ack_count,"
                      "average_cw,expected_backoff_s,rts_s,cts_s,tcp_frame_s,mac_ack_s,"
                      "interframe_s,current_cw_ns3,current_cw_slots,RBRS_over_FBRS,"
                      "cw_prime_raw_slots,cw_prime_slots,cw_prime_ns3,decision\n";
        }
        for (uint32_t index = 0; index < devices.GetN(); ++index)
        {
            Ptr<WifiNetDevice> device = DynamicCast<WifiNetDevice>(devices.Get(index));
            devicesByAddress.emplace(Mac48Address::ConvertFrom(device->GetAddress()), device);
        }
        // The console tag reflects what is actually happening. With CATRA control
        // off this loop only *measures* channel access; the CW' decision is a
        // preview that is not applied to the live Txop.
        const std::string measurementTag =
            catraControlEnabled ? "[CATRA-STATION]" : "[MAC-CHANNEL-ACCESS]";
        const std::string decisionMode = catraControlEnabled ? "applied" : "preview-not-applied";
        for (uint32_t index = 0; index < devices.GetN(); ++index)
        {
            Ptr<WifiNetDevice> localDevice = DynamicCast<WifiNetDevice>(devices.Get(index));
            auto report =
                [&, index, counts, localDevice, measurementTag, decisionMode, catraControlEnabled,
                 standardCwMinNs3, standardCwMaxNs3](const CatraActiveTimeSample& sample) {
                const auto& flowCounts = counts.at(index);
                const uint64_t packetCount = sample.tcpDataPackets + sample.tcpAckPackets;
                const bool valid = std::isfinite(sample.realBandwidthRatio) &&
                                   sample.realBandwidthRatio >= 0.0 && flowCounts.nTotal > 0;
                const uint32_t currentCwNs3 = localDevice->GetMac()->GetTxop()->GetCw(0);
                const uint32_t currentCwSlots = currentCwNs3 + 1;
                // Eq. (5) multiplies the *original* back-off window, not the CW
                // that CATRA pinned in the previous period. When control is on
                // and we pin cwMin=cwMax=CW', GetCw() would return that pinned
                // value, so feeding it back would shrink CW geometrically. Use
                // the standard CWmin as the stable base so CW' can rise again if
                // the station later exceeds its fair share. In preview mode
                // (control off) the live CW is the honest base.
                const uint32_t baseCwNs3 = catraControlEnabled ? standardCwMinNs3 : currentCwNs3;
                const CatraMacDecision macDecision = CalculateCatraMacDecision(
                    baseCwNs3,
                    standardCwMaxNs3,
                    flowCounts.fairBandwidthRatio,
                    sample.realBandwidthRatio);
                measurementPassed = measurementPassed && valid;

                // Apply CATRA channel access control (paper Eq. 5) to the live
                // MAC only when control is enabled and this station actually
                // sends flows (FBRS > 0). Setting both cwMin and cwMax to the
                // CATRA value pins the window so BEB cannot escalate away from
                // it before the next EP; the setters call ResetCw internally so
                // the next backoff uses the new value immediately. Stations with
                // FBRS = 0 (the receiver R) keep the standard DCF/BEB window.
                if (catraControlEnabled && valid && flowCounts.fairBandwidthRatio > 0.0 &&
                    macDecision.action != CatraMacAction::NO_DATA_SEND_FLOW)
                {
                    Ptr<Txop> txop = localDevice->GetMac()->GetTxop();
                    txop->SetMinCw(macDecision.ns3Cw, 0);
                    txop->SetMaxCw(macDecision.ns3Cw, 0);
                }
                std::ofstream output(stationCsvPath, std::ios::app);
                output << std::fixed << std::setprecision(6) << sample.periodEnd.GetSeconds() << ','
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
                       << currentCwSlots << ',' << macDecision.ratio << ','
                       << macDecision.rawWindowSlots << ',' << macDecision.windowSlots << ','
                       << macDecision.ns3Cw << ',' << ToString(macDecision.action) << '\n';
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
              << " applications=" << (routeProbeEnabled ? 6 : baselineEnabled ? 4 : 0)
              << " traffic="
              << (routeProbeEnabled ? "udp-route-probes"
                                    : baselineEnabled ? "two-saturated-tcp-flows" : "none")
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
    Scenario1BaselineApplications baselineApplications;
    ApplicationContainer contentionApplications;
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
                  << "tcp=TcpTahoe implementation=CatraTcpTahoe+TahoeRecovery sack=false"
                  << " classification=PORT payload_bytes=" << SCENARIO1_TCP_PAYLOAD_BYTES
                  << " traffic_start_s=" << trafficStartS
                  << " simulation_stop_s=" << simulationTimeS << "\n";
        baselineApplications = InstallScenario1Baseline(
            nodes, interfaces, trafficStartS, simulationTimeS);
        if (enableContention)
        {
            contentionApplications = InstallScenario1ContentionTraffic(
                nodes, interfaces, trafficStartS, simulationTimeS);
        }
        else
        {
            std::cout << "[CONTENTION-LOAD] enabled=false"
                      << " note=original_tcp_flows_still_use_dcf\n";
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
                                       trafficStartS,
                                       simulationTimeS,
                                       csvPath,
                                       flow2ForwardedByNode) &&
         ValidateScenario1Baseline(
             flowMonitor, flowClassifier, stationCount, flow2ForwardedByNode));
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
              << "next_phase="
              << (measurementEnabled
                      ? "integrate-catra-mac-write-hook"
                      : baselineEnabled ? "integrate-read-only-catra-measurements"
                      : routeProbeEnabled ? "original-tcp-baseline"
                                          : "install-static-host-routes-and-validate-with-udp")
              << "\n";

    Simulator::Destroy();
    return strict && !overallPassed ? 1 : 0;
}
