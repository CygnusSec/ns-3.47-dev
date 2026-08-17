/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// This executable calibrates the PHY assumptions used by CATRA Scenario 1.
// One transmitter sends 802.11b broadcast data frames to receivers placed at
// the acceptance distances and at the nominal 250 m/550 m range boundaries.
// A successful MAC reception proves decoding, while the PHY state trace
// distinguishes reception from carrier-sense-only energy.

#include "ns3/core-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/propagation-module.h"
#include "ns3/wifi-module.h"

#include <array>
#include <iomanip>
#include <iostream>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("CatraPhyRangeProbe");

namespace
{

constexpr uint32_t TRANSMITTER_INDEX = 0;
constexpr std::array<double, 6> DISTANCES_M{0.0, 200.0, 250.0, 400.0, 550.0, 600.0};
constexpr uint32_t NODE_COUNT = DISTANCES_M.size();

// Store only the observations needed by the acceptance matrix.  TX/IDLE state
// durations are deliberately excluded because they do not distinguish a
// decodable receiver from a carrier-sense-only receiver.
struct PhyObservation
{
    uint32_t macRxPackets{0};
    uint32_t rxEvents{0};
    uint32_t ccaBusyEvents{0};
    Time rxTime{Seconds(0)};
    Time ccaBusyTime{Seconds(0)};
};

std::array<PhyObservation, NODE_COUNT> g_observations;
bool g_verboseEvents{true};

/**
 * Extract the node ID inserted by Config::Connect into a trace context.
 *
 * A context looks like "/NodeList/2/DeviceList/0/...".  Parsing it here lets
 * one callback collect observations for every receiver without registering a
 * different bound callback for each node.
 */
uint32_t
GetNodeIdFromContext(const std::string& context)
{
    const std::string marker = "/NodeList/";
    const std::size_t begin = context.find(marker);
    NS_ABORT_MSG_IF(begin == std::string::npos, "Trace context has no NodeList component");

    const std::size_t valueBegin = begin + marker.size();
    const std::size_t valueEnd = context.find('/', valueBegin);
    NS_ABORT_MSG_IF(valueEnd == std::string::npos, "Trace context has no node-id terminator");
    return static_cast<uint32_t>(std::stoul(context.substr(valueBegin, valueEnd - valueBegin)));
}

void
MacRxTrace(std::string context, Ptr<const Packet> packet)
{
    const uint32_t nodeId = GetNodeIdFromContext(context);
    if (nodeId >= g_observations.size() || nodeId == TRANSMITTER_INDEX)
    {
        return;
    }

    // MacRx is emitted only after the complete Wi-Fi frame has passed PHY and
    // MAC processing.  It is therefore the probe's evidence of successful
    // decoding; an RX PHY state by itself is not sufficient evidence.
    ++g_observations[nodeId].macRxPackets;
    if (g_verboseEvents)
    {
        std::cout << "[MAC-RX] t=" << Simulator::Now().GetSeconds() << "s"
                  << " node=n" << nodeId << " distance_m=" << DISTANCES_M[nodeId]
                  << " uid=" << packet->GetUid() << " bytes=" << packet->GetSize()
                  << " decoded_count=" << g_observations[nodeId].macRxPackets << "\n";
    }
}

/**
 * Accumulate the PHY states that identify reception and carrier sensing.
 *
 * RX means the PHY attempted to receive a PPDU.  CCA_BUSY means the channel
 * was unavailable because detectable energy or a Wi-Fi preamble was present.
 * The final classification combines these state durations with MacRx rather
 * than assuming that every RX event was decoded successfully.
 */
void
PhyStateTrace(std::string context, Time start, Time duration, WifiPhyState state)
{
    const uint32_t nodeId = GetNodeIdFromContext(context);
    if (nodeId >= g_observations.size() || nodeId == TRANSMITTER_INDEX)
    {
        return;
    }

    PhyObservation& observation = g_observations[nodeId];
    if (state == WifiPhyState::RX)
    {
        ++observation.rxEvents;
        observation.rxTime += duration;
        if (g_verboseEvents)
        {
            std::cout << "[PHY-RX] t=" << start.GetSeconds() << "s"
                      << " node=n" << nodeId << " distance_m=" << DISTANCES_M[nodeId]
                      << " duration_us=" << duration.GetMicroSeconds()
                      << " cumulative_us=" << observation.rxTime.GetMicroSeconds() << "\n";
        }
    }
    else if (state == WifiPhyState::CCA_BUSY)
    {
        ++observation.ccaBusyEvents;
        observation.ccaBusyTime += duration;
        if (g_verboseEvents)
        {
            std::cout << "[PHY-CCA] t=" << start.GetSeconds() << "s"
                      << " node=n" << nodeId << " distance_m=" << DISTANCES_M[nodeId]
                      << " duration_us=" << duration.GetMicroSeconds()
                      << " cumulative_us=" << observation.ccaBusyTime.GetMicroSeconds() << "\n";
        }
    }
}

/** Send one layer-2 broadcast frame from the calibration transmitter. */
void
SendProbe(Ptr<NetDevice> transmitter, uint32_t sequence, uint32_t packetSize)
{
    Ptr<Packet> packet = Create<Packet>(packetSize);
    const bool accepted =
        transmitter->Send(packet, Mac48Address::GetBroadcast(), 0x88b5);
    NS_ABORT_MSG_IF(!accepted, "The Wi-Fi device rejected probe packet " << sequence);

    if (g_verboseEvents)
    {
        std::cout << "[PROBE-TX] t=" << Simulator::Now().GetSeconds() << "s"
                  << " node=n" << TRANSMITTER_INDEX << " sequence=" << sequence
                  << " uid=" << packet->GetUid() << " bytes=" << packetSize
                  << " destination=broadcast\n";
    }
}

/**
 * Force one final PHY transition so the State trace emits its last interval.
 *
 * WifiPhyStateHelper reports completed state intervals when a later state
 * transition occurs.  Without this post-measurement transition, the final
 * CCA_BUSY interval at a distant receiver would remain valid internally but
 * would not appear in the trace counters before Simulator::Stop.
 */
void
FlushReceiverStateTraces(NetDeviceContainer devices)
{
    for (uint32_t nodeId = 1; nodeId < devices.GetN(); ++nodeId)
    {
        Ptr<WifiNetDevice> wifiDevice = DynamicCast<WifiNetDevice>(devices.Get(nodeId));
        NS_ABORT_MSG_IF(!wifiDevice, "Expected a WifiNetDevice while flushing PHY traces");
        wifiDevice->GetPhy()->SetSleepMode();
    }

    if (g_verboseEvents)
    {
        std::cout << "[TRACE-FLUSH] t=" << Simulator::Now().GetSeconds()
                  << "s transitioned all receiver PHYs to sleep\n";
    }
}

const char*
PassFail(bool condition)
{
    return condition ? "PASS" : "FAIL";
}

} // namespace

int
main(int argc, char* argv[])
{
    uint32_t packetSize{1024};
    uint32_t packetCount{10};
    Time packetInterval{MilliSeconds(100)};
    double txPowerDbm{16.0};
    double rxSensitivityDbm{-87.0};
    double ccaEdThresholdDbm{-87.0};
    double ccaSensitivityDbm{-75.0};
    double rxNoiseFigureDb{18.0};
    double antennaHeightM{1.5};
    double systemLoss{1.0};
    uint32_t seed{1};
    uint64_t run{1};
    bool enablePcap{false};
    bool strict{true};
    bool verboseEvents{true};

    // Every calibration input is exposed on the command line so experiments
    // can test alternatives without silently changing the source-of-truth
    // defaults recorded in the CATRA directory's README.md.
    CommandLine cmd(__FILE__);
    cmd.AddValue("packetSize", "Probe payload size in bytes", packetSize);
    cmd.AddValue("packetCount", "Number of broadcast probe frames", packetCount);
    cmd.AddValue("packetInterval", "Time between probe frames", packetInterval);
    cmd.AddValue("txPower", "Fixed transmit power in dBm", txPowerDbm);
    cmd.AddValue("rxSensitivity", "PHY decode sensitivity in dBm", rxSensitivityDbm);
    cmd.AddValue("ccaEdThreshold", "PHY energy-detection CCA threshold in dBm", ccaEdThresholdDbm);
    cmd.AddValue("ccaSensitivity", "Wi-Fi preamble CCA sensitivity in dBm", ccaSensitivityDbm);
    cmd.AddValue("rxNoiseFigure", "Receiver noise figure in dB", rxNoiseFigureDb);
    cmd.AddValue("antennaHeight", "Two-Ray antenna height above node Z in meters", antennaHeightM);
    cmd.AddValue("systemLoss", "Two-Ray dimensionless system loss", systemLoss);
    cmd.AddValue("seed", "Random-number seed", seed);
    cmd.AddValue("run", "Random-number run number", run);
    cmd.AddValue("enablePcap", "Write one PCAP file per Wi-Fi device", enablePcap);
    cmd.AddValue("strict", "Return a failure status unless the acceptance matrix passes", strict);
    cmd.AddValue("verboseEvents", "Print per-frame MAC and PHY state events", verboseEvents);
    cmd.Parse(argc, argv);

    NS_ABORT_MSG_IF(packetCount == 0, "packetCount must be greater than zero");
    NS_ABORT_MSG_IF(packetSize == 0, "packetSize must be greater than zero");
    NS_ABORT_MSG_IF(packetInterval <= Time(0), "packetInterval must be positive");
    NS_ABORT_MSG_IF(systemLoss < 1.0, "systemLoss must be at least 1.0");
    g_verboseEvents = verboseEvents;

    std::cout << "\n=== 1. Validate calibration inputs ===\n"
              << "packet_count=" << packetCount << " packet_size=" << packetSize
              << " interval_s=" << packetInterval.GetSeconds() << " strict=" << std::boolalpha
              << strict << " verbose_events=" << verboseEvents << "\n";

    // Explicit seed/run values make future stochastic PHY configurations
    // reproducible even though the current Two-Ray model is deterministic.
    RngSeedManager::SetSeed(seed);
    RngSeedManager::SetRun(run);

    Config::SetDefault("ns3::WifiRemoteStationManager::NonUnicastMode",
                       StringValue("DsssRate11Mbps"));

    std::cout << "\n=== 2. Create transmitter and range-boundary receivers ===\n";
    NodeContainer nodes;
    nodes.Create(NODE_COUNT);

    // Node zero is the transmitter at the origin.  The remaining positions
    // include both the three required acceptance points and the nominal range
    // boundaries stated by the paper.
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positions = CreateObject<ListPositionAllocator>();
    for (double distance : DISTANCES_M)
    {
        positions->Add(Vector(distance, 0.0, 0.0));
    }
    mobility.SetPositionAllocator(positions);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);
    for (uint32_t nodeId = 0; nodeId < NODE_COUNT; ++nodeId)
    {
        std::cout << "node=n" << nodeId << " role="
                  << (nodeId == TRANSMITTER_INDEX ? "transmitter" : "receiver")
                  << " position=(" << DISTANCES_M[nodeId] << ",0,0)m\n";
    }

    std::cout << "\n=== 3. Configure Two-Ray propagation and 802.11b PHY ===\n";
    // TwoRayGroundPropagationLossModel defaults to 5.15 GHz, so the frequency
    // must be set explicitly to the center frequency of 802.11b channel 1.
    // HeightAboveZ avoids zero-height antennas, for which the ground-reflection
    // term would not represent the intended propagation geometry.
    Ptr<TwoRayGroundPropagationLossModel> loss =
        CreateObject<TwoRayGroundPropagationLossModel>();
    loss->SetAttribute("Frequency", DoubleValue(2.412e9));
    loss->SetAttribute("SystemLoss", DoubleValue(systemLoss));
    loss->SetAttribute("HeightAboveZ", DoubleValue(antennaHeightM));

    Ptr<YansWifiChannel> channel = CreateObject<YansWifiChannel>();
    channel->SetPropagationDelayModel(CreateObject<ConstantSpeedPropagationDelayModel>());
    channel->SetPropagationLossModel(loss);

    // YansWifiChannel discards signals below RxSensitivity before passing them
    // to the Wi-Fi PHY state machine.  The calibrated -87 dBm sensitivity lets
    // the 550 m signal enter that state machine while rejecting the 600 m
    // signal.  The noise figure then separates successful decoding at 250 m
    // from carrier-sense-only behavior at 400 m and 550 m.
    YansWifiPhyHelper phy;
    phy.SetChannel(channel);
    phy.Set("ChannelSettings", StringValue("{1, 22, BAND_2_4GHZ, 0}"));
    phy.Set("TxPowerStart", DoubleValue(txPowerDbm));
    phy.Set("TxPowerEnd", DoubleValue(txPowerDbm));
    phy.Set("RxSensitivity", DoubleValue(rxSensitivityDbm));
    phy.Set("CcaEdThreshold", DoubleValue(ccaEdThresholdDbm));
    phy.Set("CcaSensitivity", DoubleValue(ccaSensitivityDbm));
    phy.Set("RxNoiseFigure", DoubleValue(rxNoiseFigureDb));
    phy.Set("RxGain", DoubleValue(0.0));
    phy.Set("TxGain", DoubleValue(0.0));
    std::cout << "standard=802.11b channel=1 frequency_hz=2412000000"
              << " mode=DsssRate11Mbps\n"
              << "tx_power_dbm=" << txPowerDbm << " rx_sensitivity_dbm=" << rxSensitivityDbm
              << " cca_ed_threshold_dbm=" << ccaEdThresholdDbm
              << " cca_sensitivity_dbm=" << ccaSensitivityDbm
              << " rx_noise_figure_db=" << rxNoiseFigureDb
              << " antenna_height_m=" << antennaHeightM << " system_loss=" << systemLoss << "\n";

    // ConstantRateWifiManager removes rate adaptation as a confounding factor.
    // Broadcast probes use NonUnicastMode, which was also fixed to 11 Mbps
    // above, so every receiver observes the same transmitted PPDU.
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
    std::cout << "installed_wifi_devices=" << devices.GetN()
              << " mac=AdhocWifiMac rate_manager=ConstantRateWifiManager\n";

    std::cout << "\n=== 4. Attach decoding and carrier-sense traces ===\n";
    // MacRx proves end-to-end frame decoding.  The PHY State trace provides
    // durations for RX attempts and CCA_BUSY intervals, including frames that
    // never reach the MAC layer.
    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MacRx",
                    MakeCallback(&MacRxTrace));
    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/State/State",
                    MakeCallback(&PhyStateTrace));

    if (enablePcap)
    {
        phy.EnablePcap("catra-phy-range-probe", devices);
        std::cout << "pcap=enabled prefix=catra-phy-range-probe\n";
    }
    else
    {
        std::cout << "pcap=disabled (use --enablePcap=true for frame inspection)\n";
    }

    std::cout << "\n=== 5. Schedule layer-2 broadcast probes ===\n";
    // Direct NetDevice transmission avoids ARP, IP routing, transport headers,
    // and applications.  This keeps the measurement isolated to Wi-Fi MAC,
    // PHY, propagation, and mobility behavior.
    const Time firstProbe{Seconds(1)};
    for (uint32_t sequence = 0; sequence < packetCount; ++sequence)
    {
        Simulator::Schedule(firstProbe + sequence * packetInterval,
                            &SendProbe,
                            devices.Get(TRANSMITTER_INDEX),
                            sequence,
                            packetSize);
    }
    // The 100 ms guard is much longer than one 1024-byte 11 Mbps PPDU.  It
    // ensures the final probe has completed before the trace-flush transition.
    const Time flushTime = firstProbe + packetCount * packetInterval + MilliSeconds(100);
    Simulator::Schedule(flushTime, &FlushReceiverStateTraces, devices);
    const Time stopTime = flushTime + MilliSeconds(400);
    Simulator::Stop(stopTime);
    std::cout << "first_probe_s=" << firstProbe.GetSeconds()
              << " trace_flush_s=" << flushTime.GetSeconds()
              << " stop_time_s=" << stopTime.GetSeconds() << "\n"
              << "\n=== 6. Run simulation and collect events ===\n";
    Simulator::Run();

    std::cout << "\n=== 7. Summarize received power and observed PHY behavior ===\n"
              << "CATRA_PHY_RANGE_PROBE\n"
              << "standard=802.11b channel=1 frequency_hz=2412000000"
              << " data_mode=DsssRate11Mbps\n"
              << "tx_power_dbm=" << txPowerDbm << " rx_sensitivity_dbm=" << rxSensitivityDbm
              << " cca_ed_threshold_dbm=" << ccaEdThresholdDbm
              << " cca_sensitivity_dbm=" << ccaSensitivityDbm
              << " rx_noise_figure_db=" << rxNoiseFigureDb
              << " antenna_height_m=" << antennaHeightM << " system_loss=" << systemLoss << "\n"
              << "packet_size=" << packetSize << " packet_count=" << packetCount
              << " interval_s=" << packetInterval.GetSeconds() << " seed=" << seed
              << " run=" << run << "\n\n";

    std::cout << std::left << std::setw(10) << "distance" << std::setw(14) << "rx_power_dbm"
              << std::setw(12) << "mac_rx" << std::setw(12) << "rx_events" << std::setw(16)
              << "cca_events" << std::setw(14) << "rx_time_us" << std::setw(16)
              << "cca_time_us" << "classification\n";

    std::array<bool, NODE_COUNT> decoded{};
    std::array<bool, NODE_COUNT> carrierBusy{};
    Ptr<MobilityModel> transmitterMobility = nodes.Get(TRANSMITTER_INDEX)->GetObject<MobilityModel>();

    for (uint32_t nodeId = 1; nodeId < NODE_COUNT; ++nodeId)
    {
        const PhyObservation& observation = g_observations[nodeId];
        const double rxPowerDbm = loss->CalcRxPower(
            txPowerDbm,
            transmitterMobility,
            nodes.Get(nodeId)->GetObject<MobilityModel>());
        decoded[nodeId] = observation.macRxPackets > 0;
        carrierBusy[nodeId] = observation.ccaBusyTime > Time(0);

        // Classification deliberately prioritizes successful MAC decoding.  A
        // decoded frame may also generate CCA_BUSY transitions.  Only a node
        // with no decoded frame and nonzero CCA_BUSY time is "cca-only".
        std::string classification = "none";
        if (decoded[nodeId])
        {
            classification = "decode";
        }
        else if (carrierBusy[nodeId])
        {
            classification = "cca-only";
        }

        std::cout << std::left << std::setw(10) << DISTANCES_M[nodeId] << std::setw(14)
                  << std::fixed << std::setprecision(3) << rxPowerDbm << std::setw(12)
                  << observation.macRxPackets << std::setw(12) << observation.rxEvents
                  << std::setw(16) << observation.ccaBusyEvents << std::setw(14)
                  << observation.rxTime.GetMicroSeconds() << std::setw(16)
                  << observation.ccaBusyTime.GetMicroSeconds() << classification << "\n";
    }

    // The strict matrix checks the paper's required behavior and the two
    // nominal boundaries.  The 600 m check also rejects a hidden RX attempt,
    // even if that attempt did not produce a decoded MAC frame.
    const bool pass200 = decoded[1];
    const bool pass250 = decoded[2];
    const bool pass400 = !decoded[3] && carrierBusy[3];
    const bool pass550 = !decoded[4] && carrierBusy[4];
    const bool pass600 =
        !decoded[5] && !carrierBusy[5] && g_observations[5].rxTime.IsZero();
    const bool allPassed = pass200 && pass250 && pass400 && pass550 && pass600;

    std::cout << "\n=== 8. Evaluate strict acceptance matrix ===\n"
              << "ACCEPTANCE_MATRIX\n"
              << "200m_decode=" << PassFail(pass200) << "\n"
              << "250m_decode_boundary=" << PassFail(pass250) << "\n"
              << "400m_cca_only=" << PassFail(pass400) << "\n"
              << "550m_cca_boundary=" << PassFail(pass550) << "\n"
              << "600m_no_detection=" << PassFail(pass600) << "\n"
              << "overall=" << PassFail(allPassed) << "\n";

    Simulator::Destroy();
    return strict && !allPassed ? 1 : 0;
}
