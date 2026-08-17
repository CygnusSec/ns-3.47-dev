/*
 * Copyright (c) 2009 The Boeing Company
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

// This script configures two nodes on an 802.11b physical layer, with
// 802.11b NICs in adhoc mode, and by default, sends one packet of 1000
// (application) bytes to the other node.  The physical layer is configured
// to receive at a fixed RSS (regardless of the distance and transmit
// power); therefore, changing position of the nodes has no effect.
//
// There are a number of command-line options available to control
// the default behavior.  The list of available command-line options
// can be listed with the following command:
// ./ns3 run "wifi-simple-adhoc --help"
//
// For instance, for this configuration, the physical layer will
// stop successfully receiving packets when rss drops below -97 dBm.
// To see this effect, try running:
//
// ./ns3 run "wifi-simple-adhoc --rss=-97 --numPackets=20"
// ./ns3 run "wifi-simple-adhoc --rss=-98 --numPackets=20"
// ./ns3 run "wifi-simple-adhoc --rss=-99 --numPackets=20"
//
// Note that all ns-3 attributes (not just the ones exposed in the below
// script) can be changed at command line; see the documentation.
//
// This script can also be helpful to put the Wifi layer into verbose
// logging mode; this command will turn on all wifi logging:
//
// ./ns3 run "wifi-simple-adhoc --verbose=1"
//
// When you are done, you will notice two pcap trace files in your directory.
// If you have tcpdump installed, you can try this:
//
// tcpdump -r wifi-simple-adhoc-0-0.pcap -nn -tt
//

#include "ns3/command-line.h"
#include "ns3/config.h"
#include "ns3/double.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/log.h"
#include "ns3/mobility-helper.h"
#include "ns3/mobility-model.h"
#include "ns3/simulation-debug-helper.h"
#include "ns3/string.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/yans-wifi-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WifiSimpleAdhoc");

// Small application-level summary.  Keeping these counters next to the
// callbacks makes it easy to compare the requested traffic with the traffic
// that actually crossed the UDP sockets.
static uint32_t g_packetsSent{0};
static uint32_t g_packetsReceived{0};
static uint64_t g_bytesSent{0};
static uint64_t g_bytesReceived{0};

/**
 * Function called when a packet is received.
 *
 * @param socket The receiving socket.
 */
void
ReceivePacket(Ptr<Socket> socket)
{
    Address sender;
    Ptr<Packet> packet;
    while ((packet = socket->RecvFrom(sender)))
    {
        ++g_packetsReceived;
        g_bytesReceived += packet->GetSize();

        InetSocketAddress senderAddress = InetSocketAddress::ConvertFrom(sender);
        NS_LOG_UNCOND("[APP-RX] t=" << Simulator::Now().GetSeconds() << "s"
                                    << " node=n" << socket->GetNode()->GetId()
                                    << " from=" << senderAddress.GetIpv4() << ":"
                                    << senderAddress.GetPort() << " uid=" << packet->GetUid()
                                    << " bytes=" << packet->GetSize()
                                    << " received=" << g_packetsReceived);
    }
}

/**
 * Generate traffic.
 *
 * @param socket The sending socket.
 * @param pktSize The packet size.
 * @param pktCount The packet count.
 * @param pktInterval The interval between two packets.
 */
static void
GenerateTraffic(Ptr<Socket> socket, uint32_t pktSize, uint32_t pktCount, Time pktInterval)
{
    if (pktCount > 0)
    {
        Ptr<Packet> packet = Create<Packet>(pktSize);
        const int bytesAccepted = socket->Send(packet);
        if (bytesAccepted >= 0)
        {
            ++g_packetsSent;
            g_bytesSent += static_cast<uint32_t>(bytesAccepted);
        }

        NS_LOG_UNCOND("[APP-TX] t=" << Simulator::Now().GetSeconds() << "s"
                                    << " node=n" << socket->GetNode()->GetId()
                                    << " uid=" << packet->GetUid() << " requestedBytes=" << pktSize
                                    << " acceptedBytes=" << bytesAccepted
                                    << " packetsRemaining=" << (pktCount - 1));

        // Each call sends one packet and schedules the next call as a future
        // discrete event; no thread sleeps between packets.
        Simulator::Schedule(pktInterval,
                            &GenerateTraffic,
                            socket,
                            pktSize,
                            pktCount - 1,
                            pktInterval);
    }
    else
    {
        NS_LOG_UNCOND("[APP-TX] t=" << Simulator::Now().GetSeconds()
                                    << "s sender finished; closing UDP socket");
        socket->Close();
    }
}

int
main(int argc, char* argv[])
{
    std::string phyMode("DsssRate1Mbps");
    dBm_u rss{-80};
    uint32_t packetSize{1000}; // bytes
    uint32_t numPackets{1};
    Time interPacketInterval{"1s"};
    bool verbose{false};
    bool tracePackets{true};
    bool printTopology{true};

    CommandLine cmd(__FILE__);
    cmd.AddValue("phyMode", "Wifi Phy mode", phyMode);
    cmd.AddValue("rss", "received signal strength", rss);
    cmd.AddValue("packetSize", "size of application packet sent", packetSize);
    cmd.AddValue("numPackets", "number of packets generated", numPackets);
    cmd.AddValue("interval", "interval between packets", interPacketInterval);
    cmd.AddValue("verbose", "turn on all WifiNetDevice log components", verbose);
    cmd.AddValue("tracePackets", "print IPv4 SEND/FORWARD/DELIVER events", tracePackets);
    cmd.AddValue("printTopology", "print the discovered topology before running", printTopology);
    cmd.Parse(argc, argv);

    NS_LOG_UNCOND("\n=== 1. Parsed simulation configuration ===");
    NS_LOG_UNCOND("standard=802.11b phyMode=" << phyMode << " fixedRss=" << rss);
    NS_LOG_UNCOND("packetSize=" << packetSize << " numPackets=" << numPackets
                                << " interval=" << interPacketInterval.GetSeconds() << "s");

    // Fix non-unicast data rate to be the same as that of unicast
    Config::SetDefault("ns3::WifiRemoteStationManager::NonUnicastMode", StringValue(phyMode));

    // STEP 2: Create two empty nodes.  Protocol stacks, Wi-Fi devices and
    // applications/sockets are installed in later, separate stages.
    NS_LOG_UNCOND("\n=== 2. Create nodes ===");
    NodeContainer c;
    c.Create(2);
    NS_LOG_UNCOND("created node n" << c.Get(0)->GetId() << " (receiver) and n"
                                   << c.Get(1)->GetId() << " (sender)");

    // The below set of helpers will help us to put together the wifi NICs we want
    // STEP 3: Build an 802.11b ad-hoc network.  AdhocWifiMac has no access
    // point or association phase; both stations participate as peers.
    NS_LOG_UNCOND("\n=== 3. Configure Wi-Fi PHY, channel and ad-hoc MAC ===");
    WifiHelper wifi;
    if (verbose)
    {
        WifiHelper::EnableLogComponents(); // Turn on all Wifi logging
    }
    wifi.SetStandard(WIFI_STANDARD_80211b);

    YansWifiPhyHelper wifiPhy;
    // This is one parameter that matters when using FixedRssLossModel
    // set it to zero; otherwise, gain will be added
    wifiPhy.Set("RxGain", DoubleValue(0));
    // ns-3 supports RadioTap and Prism tracing extensions for 802.11b
    wifiPhy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);

    YansWifiChannelHelper wifiChannel;
    wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    // The below FixedRssLossModel will cause the rss to be fixed regardless
    // of the distance between the two stations, and the transmit power
    wifiChannel.AddPropagationLoss("ns3::FixedRssLossModel", "Rss", DoubleValue(rss));
    wifiPhy.SetChannel(wifiChannel.Create());
    NS_LOG_UNCOND("channel=YansWifiChannel delay=constant-speed loss=fixed-rss(" << rss
                                                                                << ")");

    // Add a mac and disable rate control
    WifiMacHelper wifiMac;
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode",
                                 StringValue(phyMode),
                                 "ControlMode",
                                 StringValue(phyMode));
    // Set it to adhoc mode
    wifiMac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, c);
    NS_LOG_UNCOND("installed " << devices.GetN()
                               << " WifiNetDevices with constant data/control rate " << phyMode);

    // Note that with FixedRssLossModel, the positions below are not
    // used for received signal strength.
    // STEP 4: Positions are still useful for topology inspection, but the
    // FixedRssLossModel deliberately makes distance irrelevant to reception.
    NS_LOG_UNCOND("\n=== 4. Install constant-position mobility ===");
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0.0, 0.0, 0.0));
    positionAlloc->Add(Vector(5.0, 0.0, 0.0));
    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(c);
    NS_LOG_UNCOND("n0=(0,0,0), n1=(5,0,0); positions do not alter fixed RSS");

    // STEP 5: Install IPv4/UDP support and assign one address per Wi-Fi NIC.
    NS_LOG_UNCOND("\n=== 5. Install Internet stack and assign IPv4 addresses ===");
    InternetStackHelper internet;
    internet.Install(c);

    Ipv4AddressHelper ipv4;
    NS_LOG_INFO("Assign IP Addresses.");
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer i = ipv4.Assign(devices);
    NS_LOG_UNCOND("n0=" << i.GetAddress(0) << "/24, n1=" << i.GetAddress(1) << "/24");

    // STEP 6: n0 listens on UDP port 80.  n1 sends link-subnet broadcast
    // packets, demonstrating that ad-hoc peers communicate without an AP.
    NS_LOG_UNCOND("\n=== 6. Create and connect UDP sockets ===");
    TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
    Ptr<Socket> recvSink = Socket::CreateSocket(c.Get(0), tid);
    InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), 80);
    recvSink->Bind(local);
    recvSink->SetRecvCallback(MakeCallback(&ReceivePacket));

    Ptr<Socket> source = Socket::CreateSocket(c.Get(1), tid);
    InetSocketAddress remote = InetSocketAddress(Ipv4Address("255.255.255.255"), 80);
    source->SetAllowBroadcast(true);
    source->Connect(remote);
    NS_LOG_UNCOND("receiver=n0 0.0.0.0:80; sender=n1 -> 255.255.255.255:80 (broadcast)");

    // Tracing
    wifiPhy.EnablePcap("wifi-simple-adhoc", devices);
    NS_LOG_UNCOND("PCAP enabled: wifi-simple-adhoc-0-0.pcap and wifi-simple-adhoc-1-0.pcap");

    if (printTopology)
    {
        // Print model types, devices, channel membership, mobility and IP data.
        // Attribute expansion is disabled here to keep the example readable.
        SimulationDebugHelper::PrintTopology("wifi-simple-adhoc topology", false);
    }

    if (tracePackets)
    {
        // Attach at IPv4 so the output shows where the packet is created and
        // where it is delivered, independently of verbose internal Wi-Fi logs.
        SimulationDebugHelper::EnableIpv4PacketFlowTracing();
    }

    // Output what we are doing
    NS_LOG_UNCOND("\n=== 7. Schedule and run the discrete-event simulation ===");
    NS_LOG_UNCOND("first transmission at t=1s; requested packets=" << numPackets);

    Simulator::ScheduleWithContext(source->GetNode()->GetId(),
                                   Seconds(1),
                                   &GenerateTraffic,
                                   source,
                                   packetSize,
                                   numPackets,
                                   interPacketInterval);

    Simulator::Run();

    NS_LOG_UNCOND("\n=== 8. Simulation summary ===");
    NS_LOG_UNCOND("sentPackets=" << g_packetsSent << " sentBytes=" << g_bytesSent
                                  << " receivedPackets=" << g_packetsReceived
                                  << " receivedBytes=" << g_bytesReceived
                                  << " packetDeliveryRatio="
                                  << (g_packetsSent ? 100.0 * g_packetsReceived / g_packetsSent : 0.0)
                                  << "%");
    NS_LOG_UNCOND("Tip: compare --rss=-97, --rss=-98 and --rss=-99 to observe reception loss.");
    Simulator::Destroy();

    return 0;
}
