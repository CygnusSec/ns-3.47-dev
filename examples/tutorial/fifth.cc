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

NS_LOG_COMPONENT_DEFINE("FifthScriptExample");

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
 * @param oldCwnd Old congestion window.
 * @param newCwnd New congestion window.
 */
static void
CwndChange(uint32_t oldCwnd, uint32_t newCwnd)
{
    // Print simulated time and the new TCP congestion-window size.
    NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "\t" << newCwnd);
}

/**
 * Rx drop callback
 *
 * @param p The dropped packet.
 */
static void
RxDrop(Ptr<const Packet> p)
{
    // Report when the receiver's error model drops a frame at the PHY layer.
    NS_LOG_UNCOND("RxDrop at " << Simulator::Now().GetSeconds());
}

// Build a lossy TCP flow and observe congestion-window and receive-drop traces.
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

    // In the following three lines, TCP NewReno is used as the congestion
    // control algorithm, the initial congestion window of a TCP connection is
    // set to 1 packet, and the classic fast recovery algorithm is used. Note
    // that this configuration is used only to demonstrate how TCP parameters
    // can be configured in ns-3. Otherwise, it is recommended to use the default
    // settings of TCP in ns-3.
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno"));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(1));
    Config::SetDefault("ns3::TcpL4Protocol::RecoveryType",
                       TypeIdValue(TypeId::LookupByName("ns3::TcpClassicRecovery")));

    // Create the TCP sender and receiver nodes.
    NodeContainer nodes;
    nodes.Create(2);

    // Configure and install the 5 Mbps, 2 ms point-to-point link.
    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("2ms"));

    NetDeviceContainer devices;
    devices = pointToPoint.Install(nodes);

    // Randomly corrupt incoming frames at node 1 to demonstrate TCP recovery.
    Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
    em->SetAttribute("ErrorRate", DoubleValue(0.00001));
    devices.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));

    // Install TCP/IP and assign one /30 IPv4 point-to-point subnet.
    InternetStackHelper stack;
    stack.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.252");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    // Define the receiver's TCP port and the sender's destination address.
    uint16_t sinkPort = 8080;
    Address sinkAddress(InetSocketAddress(interfaces.GetAddress(1), sinkPort));
    // Listen on all node-1 IPv4 interfaces using a TCP PacketSink.
    PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory",
                                      InetSocketAddress(Ipv4Address::GetAny(), sinkPort));
    ApplicationContainer sinkApps = packetSinkHelper.Install(nodes.Get(1));
    sinkApps.Start(Seconds(0.));
    sinkApps.Stop(Seconds(20.));

    // Create the sender socket explicitly so its congestion trace is accessible.
    Ptr<Socket> ns3TcpSocket = Socket::CreateSocket(nodes.Get(0), TcpSocketFactory::GetTypeId());
    // Invoke CwndChange whenever TCP changes its congestion window.
    ns3TcpSocket->TraceConnectWithoutContext("CongestionWindow", MakeCallback(&CwndChange));

    // Configure TutorialApp to send 1000 packets of 1040 bytes at 1 Mbps.
    Ptr<TutorialApp> app = CreateObject<TutorialApp>();
    app->Setup(ns3TcpSocket, sinkAddress, 1040, 1000, DataRate("1Mbps"));
    nodes.Get(0)->AddApplication(app);
    app->SetStartTime(Seconds(1.));
    app->SetStopTime(Seconds(20.));

    // Observe frames discarded by the receiving point-to-point device.
    devices.Get(1)->TraceConnectWithoutContext("PhyRxDrop", MakeCallback(&RxDrop));

    SimulationDebugHelper::PrintTopology("ns-3 Fifth Tutorial TCP Topology", printAttributes);
    std::cout << "\nTCP flow: n0/" << interfaces.GetAddress(0) << " -> n1/"
              << interfaces.GetAddress(1) << ":" << sinkPort
              << "\nTraffic: 1000 packets x 1040 bytes at 1 Mbps"
              << "\nError rate: 0.00001; congestion algorithm: TcpNewReno\n";
    if (tracePackets)
    {
        SimulationDebugHelper::EnableIpv4PacketFlowTracing();
    }

    // Run for at most 20 simulated seconds and release global state afterward.
    Simulator::Stop(Seconds(20));
    Simulator::Run();

    Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApps.Get(0));
    std::cout << "\nSimulation summary: finished at " << Simulator::Now().GetSeconds()
              << " s, TCP sink received " << sink->GetTotalRx() << " bytes\n";
    Simulator::Destroy();

    return 0;
}
