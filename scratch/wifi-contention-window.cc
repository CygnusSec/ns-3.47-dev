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
uint32_t g_dataAttempts = 0;
uint32_t g_lastCw = 15;
bool g_demoStarted = false;
Ptr<ListErrorModel> g_apErrorModel;

void
PrintRow(const std::string& event, const std::string& details)
{
    std::cout << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << "  "
              << std::left << std::setw(14) << event << details << std::endl;
}

void
StaBackoffTrace(uint32_t slots, uint8_t /* linkId */)
{
    if (!g_demoStarted)
    {
        return;
    }
    PrintRow("BACKOFF", "slots=" + std::to_string(slots) + " selected-from=[0," +
                            std::to_string(g_lastCw) + "]");
}

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
        reason = "TX failed: binary exponential backoff";
    }
    else if (cw < g_lastCw)
    {
        reason = "TX succeeded: reset to CWmin";
    }
    else
    {
        reason = "CW initialized/reset";
    }

    PrintRow("CW", std::to_string(g_lastCw) + " -> " + std::to_string(cw) + "  " + reason);
    g_lastCw = cw;
}

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
    PrintRow("DATA attempt", "#" + std::to_string(g_dataAttempts) +
                                 (forceFailure ? " -> deliberately corrupted at AP"
                                               : " -> allowed to succeed"));

    if (forceFailure)
    {
        std::list<uint64_t> packetUids;
        for (const auto& mpdu : *PeekPointer(psdu))
        {
            packetUids.push_back(mpdu->GetPacket()->GetUid());
        }
        g_apErrorModel->SetList(packetUids);
    }
    else
    {
        g_apErrorModel->SetList({});
    }
}

void
MacRx(Ptr<const Packet> /* packet */)
{
    PrintRow("AP received", "data frame successfully");
}

void
StartDemo()
{
    g_demoStarted = true;
    PrintRow("DEMO start", "one data packet queued at the STA");
}

} // namespace

int
main(int argc, char* argv[])
{
    uint32_t seed = 1;
    uint32_t packetSize = 1000;

    CommandLine cmd(__FILE__);
    cmd.AddValue("failures", "Number of initial data transmissions deliberately corrupted",
                 g_forcedFailures);
    cmd.AddValue("packetSize", "Data packet size in bytes", packetSize);
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
    client->SetAttribute("PacketSize", UintegerValue(packetSize));
    client->SetAttribute("MaxPackets", UintegerValue(1));
    client->SetAttribute("Interval", TimeValue(Seconds(1)));
    client->SetRemote(destination);
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
    g_lastCw = staTxop->GetMinCw(0);
    staTxop->TraceConnectWithoutContext("CwTrace", MakeCallback(&StaCwTrace));
    staTxop->TraceConnectWithoutContext("BackoffTrace", MakeCallback(&StaBackoffTrace));
    staWifi->GetPhy(0)->TraceConnectWithoutContext("PhyTxPsduBegin", MakeCallback(&StaPhyTx));
    apWifi->GetMac()->TraceConnectWithoutContext("MacRx", MakeCallback(&MacRx));

    // Association happens before this point; hide its CW/backoff activity from the teaching output.
    Simulator::Schedule(Seconds(0.49), &StartDemo);

    std::cout << "IEEE 802.11a contention-window demonstration\n"
              << "CWmin=" << staTxop->GetMinCw(0) << ", CWmax=" << staTxop->GetMaxCw(0)
              << ", forced failures=" << g_forcedFailures << "\n\n"
              << "time(s)   event         details\n"
              << "-----------------------------------------------" << std::endl;

    Simulator::Stop(Seconds(2.0));
    Simulator::Run();
    Simulator::Destroy();
    return 0;
}
