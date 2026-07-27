/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "tutorial-app.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/simulation-debug-helper.h"

#include <fstream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SixthScriptExample");

// ===========================================================================
//
//         node 0                 node 1
//   +----------------+    +----------------+
//   |    ns-3 TCP    |    |    ns-3 TCP    |
//   +----------------+    +----------------+
//   |    10.1.1.1    |    |    10.1.1.2    |
//   +----------------+    +----------------+
//   | point-to-point |    | point-to-point |
//   +----------------+    +----------------+
//           |                     |
//           +---------------------+
//                5 Mbps, 2 ms
//
//
// We want to look at changes in the ns-3 TCP congestion window.  We need
// to crank up a flow and hook the CongestionWindow attribute on the socket
// of the sender.  Normally one would use an on-off application to generate a
// flow, but this has a couple of problems.  First, the socket of the on-off
// application is not created until Application Start time, so we wouldn't be
// able to hook the socket (now) at configuration time.  Second, even if we
// could arrange a call after start time, the socket is not public so we
// couldn't get at it.
//
// So, we can cook up a simple version of the on-off application that does what
// we want.  On the plus side we don't need all of the complexity of the on-off
// application.  On the minus side, we don't have a helper, so we have to get
// a little more involved in the details, but this is trivial.
//
// So first, we create a socket and do the trace connect on it; then we pass
// this socket into the constructor of our simple application which we then
// install in the source node.
// ===========================================================================
//

/**
 * Congestion window change callback
 *
 * @param stream The output stream file.
 * @param oldCwnd Old congestion window.
 * @param newCwnd New congestion window.
 */
static void
CwndChange(Ptr<OutputStreamWrapper> stream, uint32_t oldCwnd, uint32_t newCwnd)
{
    // Show the new congestion window in the console.
    NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "\t" << newCwnd);
    // Persist time, old window, and new window as tab-separated columns.
    *stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << oldCwnd << "\t" << newCwnd
                         << std::endl;
}

/**
 * Rx drop callback
 *
 * @param file The output PCAP file.
 * @param p The dropped packet.
 */
static void
RxDrop(Ptr<PcapFileWrapper> file, Ptr<const Packet> p)
{
    // Show the drop time and also encode the dropped packet in a PCAP record.
    NS_LOG_UNCOND("RxDrop at " << Simulator::Now().GetSeconds());
    file->Write(Simulator::Now(), p);
}

int
main(int argc, char* argv[])
{
    bool printAttributes = true;
    bool tracePackets = false;

    // Parse standard ns-3 command-line arguments.
    CommandLine cmd(__FILE__);
    cmd.AddValue("printAttributes", "Print readable attributes for every model", printAttributes);
    cmd.AddValue("tracePackets", "Print IPv4 TCP forwarding actions", tracePackets);
    cmd.Parse(argc, argv);

    // Create two nodes joined by a configured point-to-point channel.
    NodeContainer nodes;
    nodes.Create(2);

    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("2ms"));

    NetDeviceContainer devices;
    devices = pointToPoint.Install(nodes);

    // Add a small receive error probability to generate observable packet loss.
    Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
    em->SetAttribute("ErrorRate", DoubleValue(0.00001));
    devices.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));

    // Install TCP/IP and assign addresses from the 10.1.1.0/30 subnet.
    InternetStackHelper stack;
    stack.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.252");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    // Install a TCP sink on node 1 and run it for the entire experiment.
    uint16_t sinkPort = 8080;
    Address sinkAddress(InetSocketAddress(interfaces.GetAddress(1), sinkPort));
    PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory",
                                      InetSocketAddress(Ipv4Address::GetAny(), sinkPort));
    ApplicationContainer sinkApps = packetSinkHelper.Install(nodes.Get(1));
    sinkApps.Start(Seconds(0.));
    sinkApps.Stop(Seconds(20.));

    // Create the sender socket before the application so traces can attach to it.
    Ptr<Socket> ns3TcpSocket = Socket::CreateSocket(nodes.Get(0), TcpSocketFactory::GetTypeId());

    // Generate a 1 Mbps stream of 1000 packets, each containing 1040 bytes.
    Ptr<TutorialApp> app = CreateObject<TutorialApp>();
    app->Setup(ns3TcpSocket, sinkAddress, 1040, 1000, DataRate("1Mbps"));
    nodes.Get(0)->AddApplication(app);
    app->SetStartTime(Seconds(1.));
    app->SetStopTime(Seconds(20.));

    // Create sixth.cwnd and bind its stream into the congestion callback.
    AsciiTraceHelper asciiTraceHelper;
    Ptr<OutputStreamWrapper> stream = asciiTraceHelper.CreateFileStream("sixth.cwnd");
    ns3TcpSocket->TraceConnectWithoutContext("CongestionWindow",
                                             MakeBoundCallback(&CwndChange, stream));

    // Create sixth.pcap and bind its writer into the receive-drop callback.
    PcapHelper pcapHelper;
    Ptr<PcapFileWrapper> file =
        pcapHelper.CreateFile("sixth.pcap", std::ios::out, PcapHelper::DLT_PPP);
    devices.Get(1)->TraceConnectWithoutContext("PhyRxDrop", MakeBoundCallback(&RxDrop, file));

    SimulationDebugHelper::PrintTopology("ns-3 Sixth Tutorial TCP Topology", printAttributes);
    std::cout << "\nTCP flow: n0/" << interfaces.GetAddress(0) << " -> n1/"
              << interfaces.GetAddress(1) << ":" << sinkPort
              << "\nTrace outputs: sixth.cwnd and sixth.pcap\n";
    if (tracePackets)
    {
        SimulationDebugHelper::EnableIpv4PacketFlowTracing();
    }

    // Execute no later than 20 seconds, then clean up simulator resources.
    Simulator::Stop(Seconds(20));
    Simulator::Run();

    Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApps.Get(0));
    std::cout << "\nSimulation summary: finished at " << Simulator::Now().GetSeconds()
              << " s, TCP sink received " << sink->GetTotalRx()
              << " bytes; congestion/drop traces written to disk\n";
    Simulator::Destroy();

    return 0;
}
