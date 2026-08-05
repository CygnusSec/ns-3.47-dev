/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Import the custom traffic generator used by the TCP sender.
#include "tutorial-app.h"

// Import PacketSinkHelper and PacketSink for the receiving application.
#include "ns3/applications-module.h"
// Import Simulator, CommandLine, logging, attributes, callbacks, and smart pointers.
#include "ns3/core-module.h"
// Import the TCP/IP stack, TCP NewReno types, sockets, and IPv4 addressing helpers.
#include "ns3/internet-module.h"
// Import Node, Packet, NetDevice, containers, and RateErrorModel.
#include "ns3/network-module.h"
// Import the point-to-point device and channel helper used for the single link.
#include "ns3/point-to-point-module.h"
// Import this repository's helper for readable topology and packet-flow diagnostics.
#include "ns3/simulation-debug-helper.h"

// Import standard file-stream types used by this tutorial family for trace output.
#include <fstream>

// Make ns-3 classes directly visible in this small tutorial program.
using namespace ns3;

// Register this source file as an ns-3 logging component.
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
    // Use simulated time, not wall-clock time, so this line aligns with packet and event traces.
    NS_LOG_UNCOND("[TCP " << Simulator::Now().GetSeconds() << "s] cwnd " << oldCwnd << " -> "
                          << newCwnd << " bytes");
}

/**
 * Report a change to the slow-start threshold, in bytes.
 *
 * NewReno uses slow start while cwnd is below ssthresh and congestion avoidance once cwnd reaches
 * the threshold. A loss normally causes NewReno to compute a smaller ssthresh.
 */
static void
SsThreshChange(uint32_t oldValue, uint32_t newValue)
{
    NS_LOG_UNCOND("[TCP " << Simulator::Now().GetSeconds() << "s] ssthresh " << oldValue << " -> "
                          << newValue << " bytes");
}

/**
 * Report transitions in the congestion-control state machine.
 *
 * CA_OPEN is normal operation, CA_DISORDER indicates duplicate ACK or SACK evidence, CA_RECOVERY
 * denotes fast retransmit/recovery, and CA_LOSS denotes timeout-driven loss recovery.
 */
static void
CongStateChange(TcpSocketState::TcpCongState_t oldState, TcpSocketState::TcpCongState_t newState)
{
    NS_LOG_UNCOND("[TCP " << Simulator::Now().GetSeconds() << "s] congestion state "
                          << TcpSocketState::TcpCongStateName[oldState] << " -> "
                          << TcpSocketState::TcpCongStateName[newState]);
}

/**
 * Report transitions in the TCP connection state machine, such as CLOSED -> SYN_SENT ->
 * ESTABLISHED. This state machine is separate from the congestion-control state machine above.
 */
static void
ConnectionStateChange(TcpSocket::TcpStates_t oldState, TcpSocket::TcpStates_t newState)
{
    NS_LOG_UNCOND("[TCP " << Simulator::Now().GetSeconds() << "s] connection state "
                          << TcpSocket::TcpStateName[oldState] << " -> "
                          << TcpSocket::TcpStateName[newState]);
}

/**
 * Report changes to the smoothed round-trip-time estimate.
 *
 * TCP derives this estimate from acknowledged transmissions and uses it to calculate the RTO.
 */
static void
RttChange(Time oldValue, Time newValue)
{
    NS_LOG_UNCOND("[TCP " << Simulator::Now().GetSeconds() << "s] smoothed RTT "
                          << oldValue.GetMilliSeconds() << " -> " << newValue.GetMilliSeconds()
                          << " ms");
}

/**
 * Report changes to the retransmission timeout.
 *
 * If an acknowledgment does not arrive before the RTO expires, TCP treats the segment as lost and
 * enters timeout-based recovery, which is more severe than fast recovery.
 */
static void
RtoChange(Time oldValue, Time newValue)
{
    NS_LOG_UNCOND("[TCP " << Simulator::Now().GetSeconds() << "s] RTO "
                          << oldValue.GetMilliSeconds() << " -> " << newValue.GetMilliSeconds()
                          << " ms");
}

/**
 * Report TCP's estimate of unacknowledged data currently in the network.
 *
 * The sender is congestion-window limited when bytes in flight reaches cwnd.
 */
static void
BytesInFlightChange(uint32_t oldValue, uint32_t newValue)
{
    NS_LOG_UNCOND("[TCP " << Simulator::Now().GetSeconds() << "s] bytes in flight " << oldValue
                          << " -> " << newValue);
}

/**
 * Report an actual TCP retransmission, including its sequence number and payload size.
 *
 * The address and socket parameters are intentionally unnamed because this two-node example only
 * has one TCP flow; the sequence number uniquely identifies the retransmitted byte range here.
 */
static void
Retransmission(Ptr<const Packet> packet,
               const TcpHeader& header,
               const Address&,
               const Address&,
               Ptr<const TcpSocketBase>)
{
    NS_LOG_UNCOND("[TCP " << Simulator::Now().GetSeconds()
                          << "s] RETRANSMIT seq=" << header.GetSequenceNumber()
                          << " payload=" << packet->GetSize() << " bytes");
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
    // This drop occurs below IP/TCP, so TCP learns about it indirectly through missing ACKs.
    NS_LOG_UNCOND("RxDrop at " << Simulator::Now().GetSeconds());
}

// Program entry point: configure, build, instrument, run, summarize, and destroy the simulation.
int
main(int argc, char* argv[])
{
    // Control whether PrintTopology also dumps readable attributes of every model.
    bool printAttributes = true;
    // Control the optional per-packet IPv4/TCP forwarding trace; it produces substantial output.
    bool tracePackets = false;
    // Control the additional TCP state/variable traces and internal NewReno/socket log messages.
    bool detailedLog = false;

    // Parse standard ns-3 command-line arguments.
    // Use the source filename in --help output so users can identify the owning example.
    CommandLine cmd(__FILE__);
    // Bind each command-line option directly to the corresponding local boolean.
    cmd.AddValue("printAttributes", "Print readable attributes for every model", printAttributes);
    cmd.AddValue("tracePackets", "Print IPv4 TCP forwarding actions", tracePackets);
    cmd.AddValue("detailedLog",
                 "Print TCP state, NewReno variables, RTT, RTO and retransmissions",
                 detailedLog);
    // Parse arguments before creating model objects so every option affects initial configuration.
    cmd.Parse(argc, argv);

    // Programmatic enabling means --detailedLog works without requiring an NS_LOG environment
    // value.
    if (detailedLog)
    {
        // Show NewReno's slow-start, congestion-avoidance, and ssthresh calculations.
        LogComponentEnable("TcpLinuxReno", LOG_LEVEL_DEBUG);
        // Show connection setup, ACK processing, segment transmission, and recovery decisions.
        LogComponentEnable("TcpSocketBase", LOG_LEVEL_INFO);
    }

    // In the following three lines, TCP NewReno is used as the congestion
    // control algorithm, the initial congestion window of a TCP connection is
    // set to 1 packet, and the classic fast recovery algorithm is used. Note
    // that this configuration is used only to demonstrate how TCP parameters
    // can be configured in ns-3. Otherwise, it is recommended to use the default
    // settings of TCP in ns-3.
    // Make every subsequently created TCP socket use TcpNewReno for congestion control.
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno"));
    // Start at one MSS so the exponential slow-start growth is easy to observe in the cwnd trace.
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(1));
    // Select classic fast recovery for duplicate-ACK loss rather than another recovery strategy.
    Config::SetDefault("ns3::TcpL4Protocol::RecoveryType",
                       TypeIdValue(TypeId::LookupByName("ns3::TcpClassicRecovery")));

    // Create the TCP sender and receiver nodes.
    // A container owns convenient references to both endpoints: index 0 is sender, index 1
    // receiver.
    NodeContainer nodes;
    // Create two empty nodes; protocol stacks, devices, and applications are installed below.
    nodes.Create(2);

    // Configure and install the 5 Mbps, 2 ms point-to-point link.
    // The helper is a factory/configurator for a matching device pair and their shared channel.
    PointToPointHelper pointToPoint;
    // Set serialization capacity on each device; this is link rate, not application sending rate.
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    // Set one-way propagation delay; the baseline RTT is therefore above roughly 4 ms.
    pointToPoint.SetChannelAttribute("Delay", StringValue("2ms"));

    // Install one point-to-point NetDevice per node and connect both devices to one channel.
    NetDeviceContainer devices;
    // Device index matches node index because Install receives the nodes in that order.
    devices = pointToPoint.Install(nodes);

    // Randomly corrupt incoming frames at node 1 to demonstrate TCP recovery.
    // Create a reference-counted error model that independently marks received data as corrupt.
    Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
    // Configure a low random error probability so TCP occasionally has to recover from loss.
    em->SetAttribute("ErrorRate", DoubleValue(0.00001));
    // Attach it only to node 1's receive path; corrupted frames are discarded before reaching IP.
    devices.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));

    // Install TCP/IP and assign one /30 IPv4 point-to-point subnet.
    // Prepare the standard IPv4/IPv6, ARP, UDP, and TCP protocol aggregation.
    InternetStackHelper stack;
    // Aggregate one complete Internet stack onto each node.
    stack.Install(nodes);

    // Prepare sequential IPv4 address allocation for the point-to-point interfaces.
    Ipv4AddressHelper address;
    // A /30 contains exactly the two usable addresses needed by this link.
    address.SetBase("10.1.1.0", "255.255.255.252");
    // Assign 10.1.1.1 to device 0 and 10.1.1.2 to device 1, then retain both interfaces.
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    // Define the receiver's TCP port and the sender's destination address.
    // Choose the transport-layer port on which the receiver will listen.
    uint16_t sinkPort = 8080;
    // Combine node 1's IPv4 address and TCP port into the sender's remote endpoint.
    Address sinkAddress(InetSocketAddress(interfaces.GetAddress(1), sinkPort));
    // Listen on all node-1 IPv4 interfaces using a TCP PacketSink.
    // Ask PacketSink to create a TCP socket rather than its alternative UDP socket.
    PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory",
                                      // Bind on every IPv4 interface of node 1 at port 8080.
                                      InetSocketAddress(Ipv4Address::GetAny(), sinkPort));
    // Construct the PacketSink application and aggregate it onto receiver node 1.
    ApplicationContainer sinkApps = packetSinkHelper.Install(nodes.Get(1));
    // Start listening before the sender connects at t=1 s.
    sinkApps.Start(Seconds(0.));
    // Keep the receiver alive until the global simulation stop time.
    sinkApps.Stop(Seconds(20.));

    // Create the sender socket explicitly so its congestion trace is accessible.
    // Create the sender socket now, rather than hiding it inside an application, so traces can be
    // connected before the socket starts changing state. The earlier Config defaults apply here.
    Ptr<Socket> ns3TcpSocket = Socket::CreateSocket(nodes.Get(0), TcpSocketFactory::GetTypeId());
    // Invoke CwndChange whenever TCP changes its congestion window.
    // Convert CwndChange into an ns-3 callback and subscribe it to every cwnd modification.
    ns3TcpSocket->TraceConnectWithoutContext("CongestionWindow", MakeCallback(&CwndChange));
    // Extra traces are optional because per-ACK variables can generate many thousands of lines.
    if (detailedLog)
    {
        // Observe the boundary between NewReno slow start and congestion avoidance.
        ns3TcpSocket->TraceConnectWithoutContext("SlowStartThreshold",
                                                 MakeCallback(&SsThreshChange));
        // Observe duplicate-ACK, fast-recovery, and timeout-recovery state transitions.
        ns3TcpSocket->TraceConnectWithoutContext("CongState", MakeCallback(&CongStateChange));
        // Observe the independent TCP connection state machine used by the handshake and close.
        ns3TcpSocket->TraceConnectWithoutContext("State", MakeCallback(&ConnectionStateChange));
        // Observe the filtered RTT estimate updated from acknowledgment timing.
        ns3TcpSocket->TraceConnectWithoutContext("RTT", MakeCallback(&RttChange));
        // Observe the timeout derived from smoothed RTT and RTT variation.
        ns3TcpSocket->TraceConnectWithoutContext("RTO", MakeCallback(&RtoChange));
        // Observe how much sent data is still awaiting cumulative or selective acknowledgment.
        ns3TcpSocket->TraceConnectWithoutContext("BytesInFlight",
                                                 MakeCallback(&BytesInFlightChange));
        // Observe each segment that TCP sends again after inferring loss.
        ns3TcpSocket->TraceConnectWithoutContext("Retransmission", MakeCallback(&Retransmission));
    }

    // Configure TutorialApp to send 1000 packets of 1040 bytes at 1 Mbps.
    // Construct the custom sender application through ns-3's reference-counted object system.
    Ptr<TutorialApp> app = CreateObject<TutorialApp>();
    // Give it the traced socket and request 1000 application packets of 1040 bytes at 1 Mbps.
    app->Setup(ns3TcpSocket, sinkAddress, 1040, 1000, DataRate("1Mbps"));
    // Aggregate the application onto sender node 0 so its lifecycle is scheduled by ns-3.
    nodes.Get(0)->AddApplication(app);
    // At t=1 s TutorialApp binds/connects the socket and immediately offers its first packet to
    // TCP.
    app->SetStartTime(Seconds(1.));
    // At t=20 s it cancels any pending send event and closes the socket.
    app->SetStopTime(Seconds(20.));

    // Observe frames discarded by the receiving point-to-point device.
    // Subscribe below IP/TCP so each frame rejected by RateErrorModel is visible in the timeline.
    devices.Get(1)->TraceConnectWithoutContext("PhyRxDrop", MakeCallback(&RxDrop));

    // Print nodes, applications, devices, interfaces, channels, and optionally all attributes.
    SimulationDebugHelper::PrintTopology("ns-3 Fifth Tutorial TCP Topology", printAttributes);
    // Print a compact, human-readable record of the experiment configuration.
    std::cout << "\nTCP flow: n0/" << interfaces.GetAddress(0) << " -> n1/"
              << interfaces.GetAddress(1) << ":" << sinkPort
              << "\nTraffic: 1000 packets x 1040 bytes at 1 Mbps"
              << "\nError rate: 0.00001; congestion algorithm: TcpNewReno\n";
    if (tracePackets)
    {
        // Add verbose IPv4/TCP send, forward, local-delivery, and drop diagnostics.
        SimulationDebugHelper::EnableIpv4PacketFlowTracing();
    }

    // Run for at most 20 simulated seconds and release global state afterward.
    // Schedule a hard stop so no future timer can keep the event queue running past 20 s.
    Simulator::Stop(Seconds(20));
    // Execute queued events in nondecreasing simulated-time order until the stop event fires.
    Simulator::Run();

    // Recover the concrete PacketSink type so its received-byte counter becomes accessible.
    Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApps.Get(0));
    std::cout << "\nSimulation summary: finished at " << Simulator::Now().GetSeconds()
              << " s, TCP sink received " << sink->GetTotalRx() << " bytes\n";
    // Release simulator-global events and singleton state before the process exits.
    Simulator::Destroy();

    // Report successful completion to the operating system.
    return 0;
}
