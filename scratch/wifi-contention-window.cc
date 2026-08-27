/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * A small, deterministic demonstration of the IEEE 802.11 contention window.
 * The AP deliberately corrupts the first few data-frame receptions.  The STA
 * therefore applies binary exponential backoff, and then resets its CW after
 * the first successful transmission.
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include <iomanip>
#include <iostream>
#include <list>

using namespace ns3;

namespace
{

uint32_t g_forcedFailures = 3;
uint32_t g_packetSize = 1000;
uint32_t g_dataAttempts = 0;
uint32_t g_lastCw = 15;
uint32_t g_cwMin = 15;
uint32_t g_cwMax = 1023;
uint32_t g_bebStage = 0;
bool g_demoStarted = false;
Time g_slotTime;
Time g_sifs;
Ptr<ListErrorModel> g_apErrorModel;

// Keep every teaching event on one time-aligned line.  The timestamps make it possible to compare
// the configured DIFS/backoff durations with the actual transmission times chosen by ns-3.
void
PrintRow(const std::string& event, const std::string& details)
{
    std::cout << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << "  "
              << std::left << std::setw(14) << event << details << std::endl;
}

// Txop emits BackoffTrace whenever it draws a new random backoff counter.  This callback does not
// implement backoff; it only explains the value already selected by ns-3's Wifi MAC.
void
StaBackoffTrace(uint32_t slots, uint8_t /* linkId */)
{
    if (!g_demoStarted)
    {
        return;
    }
    const Time backoffTime = slots * g_slotTime;
    PrintRow("BACKOFF draw",
             "UniformInteger[0," + std::to_string(g_lastCw) + "] -> " +
                 std::to_string(slots) + " slots");
    PrintRow("BACKOFF wait",
             std::to_string(slots) + " x " + std::to_string(g_slotTime.GetMicroSeconds()) +
                 " us = " + std::to_string(backoffTime.GetMicroSeconds()) +
                 " us (counter decrements only while channel is idle)");
}

// Txop emits CwTrace after the ACK outcome is known.  On failure, ns-3 increments the short retry
// counter (our g_bebStage display) and applies binary exponential backoff.  On success it resets CW
// to CWmin.  Again, this callback is read-only: Txop remains the source of truth for the CW value.
void
StaCwTrace(uint32_t cw, uint8_t /* linkId */)
{
    if (!g_demoStarted)
    {
        g_lastCw = cw;
        return;
    }

    std::string reason;
    if (cw > g_lastCw)
    {
        ++g_bebStage;
        const uint32_t formulaCw =
            std::min(g_cwMax, (1U << g_bebStage) * (g_cwMin + 1) - 1);
        reason = "no ACK -> BEB stage=" + std::to_string(g_bebStage) + ": min(" +
                 std::to_string(g_cwMax) + ", 2^" + std::to_string(g_bebStage) + "*(" +
                 std::to_string(g_cwMin) + "+1)-1)=" + std::to_string(formulaCw);
    }
    else if (cw < g_lastCw)
    {
        g_bebStage = 0;
        reason = "ACK received -> BEB stage=0 and CW=CWmin";
    }
    else
    {
        g_bebStage = 0;
        reason = "success while already at CWmin";
    }

    PrintRow("CW", std::to_string(g_lastCw) + " -> " + std::to_string(cw) + "  " + reason);
    g_lastCw = cw;
}

// PhyTxPsduBegin fires when the STA really starts transmitting over the channel.  To make the demo
// repeatable, the receiver's ListErrorModel corrupts the first g_forcedFailures DATA frames.  The
// resulting missing ACK drives the normal ns-3 retry/CW machinery; we never call SetCw ourselves.
void
StaPhyTx(WifiConstPsduMap psduMap, WifiTxVector /* txVector */, double /* txPowerW */)
{
    Ptr<const WifiPsdu> psdu = psduMap.cbegin()->second;
    const WifiMacHeader& header = psdu->GetHeader(0);
    if (!header.IsData())
    {
        return;
    }

    ++g_dataAttempts;
    const bool forceFailure = g_dataAttempts <= g_forcedFailures;
    PrintRow("TX DATA",
             "attempt=" + std::to_string(g_dataAttempts) + ", BEB stage=" +
                 std::to_string(g_bebStage) + ", current CW=" + std::to_string(g_lastCw));

    if (forceFailure)
    {
        PrintRow("CHANNEL",
                 "force RX error at AP -> DATA is dropped -> STA will not receive an ACK");
        std::list<uint64_t> packetUids;
        for (const auto& mpdu : *PeekPointer(psdu))
        {
            packetUids.push_back(mpdu->GetPacket()->GetUid());
        }
        g_apErrorModel->SetList(packetUids);
    }
    else
    {
        PrintRow("CHANNEL", "do not inject an error -> DATA/ACK exchange can complete");
        g_apErrorModel->SetList({});
    }
}

// A successful AP MacRx means the DATA frame survived reception.  The AP then responds with an ACK
// after SIFS; the later STA CwTrace confirms that the ACK completed the exchange.
void
MacRx(Ptr<const Packet> /* packet */)
{
    if (g_demoStarted)
    {
        PrintRow("AP MacRx", "DATA accepted; AP schedules ACK after SIFS=" +
                                 std::to_string(g_sifs.GetMicroSeconds()) + " us");
    }
}

void
AppTx(Ptr<const Packet> packet, const Address& /* destination */)
{
    PrintRow("APP -> MAC", "enqueue packet UID=" + std::to_string(packet->GetUid()) +
                                ", payload=" + std::to_string(g_packetSize) +
                                " B, after LLC/SNAP=" + std::to_string(packet->GetSize()) + " B");
}

void
StartDemo()
{
    g_demoStarted = true;
    PrintRow("DEMO ready", "the application will enqueue one packet at t=0.500000 s");
    PrintRow("DCF rule",
             "idle DIFS=" + std::to_string(g_sifs.GetMicroSeconds()) + "+2*" +
                 std::to_string(g_slotTime.GetMicroSeconds()) + "=" +
                 std::to_string((g_sifs + 2 * g_slotTime).GetMicroSeconds()) +
                 " us, then decrement the backoff once per idle slot");
}

} // namespace

int
main(int argc, char* argv[])
{
    uint32_t seed = 1;

    CommandLine cmd(__FILE__);
    cmd.AddValue("failures", "Number of initial data transmissions deliberately corrupted",
                 g_forcedFailures);
    cmd.AddValue("packetSize", "Data packet size in bytes", g_packetSize);
    cmd.AddValue("seed", "Random seed controlling the selected backoff slots", seed);
    cmd.Parse(argc, argv);

    RngSeedManager::SetSeed(seed);
    RngSeedManager::SetRun(1);

    // Avoid RTS/CTS so that each failed DATA/ACK exchange causes one visible CW increase.
    Config::SetDefault("ns3::WifiRemoteStationManager::RtsCtsThreshold",
                       StringValue("22000"));
    Config::SetDefault("ns3::WifiRemoteStationManager::FragmentationThreshold",
                       StringValue("22000"));

    NodeContainer nodes;
    nodes.Create(2); // node 0: AP, node 1: STA

    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211a);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode",
                                 StringValue("OfdmRate6Mbps"),
                                 "ControlMode",
                                 StringValue("OfdmRate6Mbps"));

    WifiMacHelper mac;
    Ssid ssid("cw-demo");
    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer apDevice = wifi.Install(phy, mac, nodes.Get(0));

    mac.SetType("ns3::StaWifiMac",
                "Ssid",
                SsidValue(ssid),
                "ActiveProbing",
                BooleanValue(false));
    NetDeviceContainer staDevice = wifi.Install(phy, mac, nodes.Get(1));

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positions = CreateObject<ListPositionAllocator>();
    positions->Add(Vector(0.0, 0.0, 0.0));
    positions->Add(Vector(1.0, 0.0, 0.0));
    mobility.SetPositionAllocator(positions);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    PacketSocketHelper packetSocket;
    packetSocket.Install(nodes);

    PacketSocketAddress destination;
    destination.SetSingleDevice(staDevice.Get(0)->GetIfIndex());
    destination.SetPhysicalAddress(apDevice.Get(0)->GetAddress());
    destination.SetProtocol(1);

    Ptr<PacketSocketClient> client = CreateObject<PacketSocketClient>();
    client->SetAttribute("PacketSize", UintegerValue(g_packetSize));
    client->SetAttribute("MaxPackets", UintegerValue(1));
    client->SetAttribute("Interval", TimeValue(Seconds(1)));
    client->SetRemote(destination);
    client->TraceConnectWithoutContext("Tx", MakeCallback(&AppTx));
    nodes.Get(1)->AddApplication(client);
    client->SetStartTime(Seconds(0.5));
    client->SetStopTime(Seconds(2.0));

    Ptr<PacketSocketServer> server = CreateObject<PacketSocketServer>();
    PacketSocketAddress local;
    local.SetSingleDevice(apDevice.Get(0)->GetIfIndex());
    local.SetProtocol(1);
    server->SetLocal(local);
    nodes.Get(0)->AddApplication(server);
    server->SetStartTime(Seconds(0.0));
    server->SetStopTime(Seconds(2.0));

    auto apWifi = DynamicCast<WifiNetDevice>(apDevice.Get(0));
    auto staWifi = DynamicCast<WifiNetDevice>(staDevice.Get(0));
    g_apErrorModel = CreateObject<ListErrorModel>();
    apWifi->GetPhy(0)->SetPostReceptionErrorModel(g_apErrorModel);

    Ptr<Txop> staTxop = staWifi->GetMac()->GetTxop();
    g_cwMin = staTxop->GetMinCw(0);
    g_cwMax = staTxop->GetMaxCw(0);
    g_lastCw = g_cwMin;
    g_slotTime = staWifi->GetPhy(0)->GetSlot();
    g_sifs = staWifi->GetPhy(0)->GetSifs();
    staTxop->TraceConnectWithoutContext("CwTrace", MakeCallback(&StaCwTrace));
    staTxop->TraceConnectWithoutContext("BackoffTrace", MakeCallback(&StaBackoffTrace));
    staWifi->GetPhy(0)->TraceConnectWithoutContext("PhyTxPsduBegin", MakeCallback(&StaPhyTx));
    apWifi->GetMac()->TraceConnectWithoutContext("MacRx", MakeCallback(&MacRx));

    // Association happens before this point; hide its CW/backoff activity from the teaching output.
    Simulator::Schedule(Seconds(0.49), &StartDemo);

    std::cout << "IEEE 802.11a DCF contention-window and BEB demonstration\n"
              << "Topology: STA --(1 m Wi-Fi link)--> AP; one non-QoS DATA packet\n"
              << "CWmin=" << g_cwMin << ", CWmax=" << g_cwMax
              << ", slot=" << g_slotTime.GetMicroSeconds()
              << " us, SIFS=" << g_sifs.GetMicroSeconds()
              << " us, forced DATA failures=" << g_forcedFailures << "\n"
              << "BEB rule after failure k: CW=min(CWmax, 2^k*(CWmin+1)-1)\n"
              << "Backoff rule: uniformly draw N in [0,CW], then wait N idle slots\n\n"
              << "time(s)   processing     details\n"
              << "-------------------------------------------------------------------------------"
              << std::endl;

    Simulator::Stop(Seconds(2.0));
    Simulator::Run();
    Simulator::Destroy();
    return 0;
}
