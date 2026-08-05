/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Import the custom application that feeds bytes into the sender TCP socket.
#include "tutorial-app.h"

// Import PacketSinkHelper and PacketSink for the TCP receiver.
#include "ns3/applications-module.h"
// Import Simulator, CommandLine, callbacks, attributes, logging, and smart pointers.
#include "ns3/core-module.h"
// Import TCP/IP, sockets, IPv4 helpers, ASCII tracing, and PCAP wrappers.
#include "ns3/internet-module.h"
// Import nodes, packets, devices, containers, and RateErrorModel.
#include "ns3/network-module.h"
// Import the helper that creates the two point-to-point devices and channel.
#include "ns3/point-to-point-module.h"
// Import this repository's readable topology and packet-flow diagnostics.
#include "ns3/simulation-debug-helper.h"

// Import std::ios::out, used when opening the PCAP output file.
#include <fstream>

// Avoid repeating the ns3:: namespace qualifier throughout this tutorial.
using namespace ns3;

// Register this translation unit as an ns-3 runtime logging component.
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
    // Print the same event immediately for interactive observation while the simulation runs.
    NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "\t" << newCwnd);
    // Get the underlying std::ostream and append one tab-separated row: time, old cwnd, new cwnd.
    *stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << oldCwnd << "\t" << newCwnd
                         << std::endl;
}

/** Report a change to TCP's slow-start threshold, in bytes. */
static void
SsThreshChange(uint32_t oldValue, uint32_t newValue)
{
    NS_LOG_UNCOND("[TCP " << Simulator::Now().GetSeconds() << "s] ssthresh " << oldValue << " -> "
                          << newValue << " bytes");
}

/** Report a transition in TCP's congestion-control state machine. */
static void
CongStateChange(TcpSocketState::TcpCongState_t oldState, TcpSocketState::TcpCongState_t newState)
{
    NS_LOG_UNCOND("[TCP " << Simulator::Now().GetSeconds() << "s] congestion state "
                          << TcpSocketState::TcpCongStateName[oldState] << " -> "
                          << TcpSocketState::TcpCongStateName[newState]);
}

/** Report a transition in the TCP connection state machine. */
static void
ConnectionStateChange(TcpSocket::TcpStates_t oldState, TcpSocket::TcpStates_t newState)
{
    NS_LOG_UNCOND("[TCP " << Simulator::Now().GetSeconds() << "s] connection state "
                          << TcpSocket::TcpStateName[oldState] << " -> "
                          << TcpSocket::TcpStateName[newState]);
}

/** Report an update to TCP's smoothed round-trip-time estimate. */
static void
RttChange(Time oldValue, Time newValue)
{
    NS_LOG_UNCOND("[TCP " << Simulator::Now().GetSeconds() << "s] smoothed RTT "
                          << oldValue.GetMilliSeconds() << " -> " << newValue.GetMilliSeconds()
                          << " ms");
}

/** Report an update to TCP's retransmission timeout. */
static void
RtoChange(Time oldValue, Time newValue)
{
    NS_LOG_UNCOND("[TCP " << Simulator::Now().GetSeconds() << "s] RTO "
                          << oldValue.GetMilliSeconds() << " -> " << newValue.GetMilliSeconds()
                          << " ms");
}

/** Report how many transmitted bytes have not yet been acknowledged. */
static void
BytesInFlightChange(uint32_t oldValue, uint32_t newValue)
{
    NS_LOG_UNCOND("[TCP " << Simulator::Now().GetSeconds() << "s] bytes in flight " << oldValue
                          << " -> " << newValue);
}

/** Report a TCP segment sent again after inferred loss or timeout. */
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
 * @param file The output PCAP file.
 * @param p The dropped packet.
 */
static void
RxDrop(Ptr<PcapFileWrapper> file, Ptr<const Packet> p)
{
    // Print the simulated drop time to correlate it with the congestion-window trace.
    NS_LOG_UNCOND("RxDrop at " << Simulator::Now().GetSeconds());
    // Append one timestamped PPP-link record containing the dropped frame to sixth.pcap.
    file->Write(Simulator::Now(), p);
}

// Program entry point: configure the model, attach file traces, run, summarize, and clean up.
int
main(int argc, char* argv[])
{
    // Control whether topology output includes every readable model attribute.
    bool printAttributes = true;
    // Control optional per-packet IPv4/TCP diagnostics; this can produce substantial output.
    bool tracePackets = false;
    // Control detailed TCP/CUBIC state tracing used to explain the complete transport workflow.
    bool detailedLog = false;

    // Parse standard ns-3 command-line arguments.
    // Include this source filename in --help output to identify the owning example.
    CommandLine cmd(__FILE__);
    // Bind each accepted option directly to the local variable that controls its behavior.
    cmd.AddValue("printAttributes", "Print readable attributes for every model", printAttributes);
    cmd.AddValue("tracePackets", "Print IPv4 TCP forwarding actions", tracePackets);
    cmd.AddValue("detailedLog",
                 "Print TCP handshake, CUBIC state, RTT, RTO, flight size and retransmissions",
                 detailedLog);
    // Parse before constructing model objects so options affect the complete setup phase.
    cmd.Parse(argc, argv);

    // Enable internal transport logs only on request; trace-file generation remains always enabled.
    if (detailedLog)
    {
        // Explain CUBIC's congestion-window and epoch calculations.
        LogComponentEnable("TcpCubic", LOG_LEVEL_DEBUG);
        // Explain handshake, ACK processing, transmission, duplicate ACKs, and loss recovery.
        LogComponentEnable("TcpSocketBase", LOG_LEVEL_INFO);
        NS_LOG_UNCOND("[WORKFLOW 0s] Detailed TCP logging enabled");
    }

    // Create two nodes joined by a configured point-to-point channel.
    // Keep convenient references to both endpoints: node 0 sends and node 1 receives.
    NodeContainer nodes;
    // Create two initially empty nodes; devices, protocol stacks, and applications follow.
    nodes.Create(2);

    // Use a helper as a factory/configurator for both NetDevices and their common channel.
    PointToPointHelper pointToPoint;
    // Configure link serialization capacity; this 5 Mbps rate is not the application rate.
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    // Configure one-way propagation delay, making the baseline RTT slightly greater than 4 ms.
    pointToPoint.SetChannelAttribute("Delay", StringValue("2ms"));

    // Retain the newly created devices; their indices match the corresponding node indices.
    NetDeviceContainer devices;
    // Install one device per node and connect the pair through one PointToPointChannel.
    devices = pointToPoint.Install(nodes);

    // Add a small receive error probability to generate observable packet loss.
    // Allocate the error model through ns-3's reference-counted object system.
    Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
    // Use a small random error probability so losses occur without dominating the transfer.
    em->SetAttribute("ErrorRate", DoubleValue(0.00001));
    // Attach it to node 1's receive path, where corrupted frames are dropped before reaching IP.
    devices.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));

    // Install TCP/IP and assign addresses from the 10.1.1.0/30 subnet.
    // Prepare the standard IPv4/IPv6, ARP, TCP, and UDP protocol aggregation.
    InternetStackHelper stack;
    // Aggregate a complete Internet stack onto both nodes.
    stack.Install(nodes);

    // Prepare sequential IPv4 allocation for the two point-to-point interfaces.
    Ipv4AddressHelper address;
    // A /30 subnet has exactly two usable addresses, which is sufficient for this link.
    address.SetBase("10.1.1.0", "255.255.255.252");
    // Assign 10.1.1.1 to node 0 and 10.1.1.2 to node 1, retaining both interfaces.
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    // Install a TCP sink on node 1 and run it for the entire experiment.
    // Select the TCP destination port on which the receiver will listen.
    uint16_t sinkPort = 8080;
    // Combine node 1's assigned IPv4 address and port into the sender's remote endpoint.
    Address sinkAddress(InetSocketAddress(interfaces.GetAddress(1), sinkPort));
    // Request a TCP-backed PacketSink rather than a UDP-backed sink.
    PacketSinkHelper packetSinkHelper(
        "ns3::TcpSocketFactory",
        // Accept connections on every node-1 IPv4 interface at port 8080.
        InetSocketAddress(Ipv4Address::GetAny(), sinkPort));
    // Construct the sink and aggregate it onto receiver node 1.
    ApplicationContainer sinkApps = packetSinkHelper.Install(nodes.Get(1));
    // Begin listening at t=0, before the sender attempts its connection at t=1.
    sinkApps.Start(Seconds(0.));
    // Keep the sink active until the experiment ends.
    sinkApps.Stop(Seconds(20.));

    // Create the sender socket before the application so traces can attach to it.
    // Create the socket explicitly so its CongestionWindow trace is accessible before connection.
    // TcpSocketFactory asks node 0's installed TCP implementation to construct the concrete socket.
    Ptr<Socket> ns3TcpSocket = Socket::CreateSocket(nodes.Get(0), TcpSocketFactory::GetTypeId());

    if (detailedLog)
    {
        NS_LOG_UNCOND("[WORKFLOW 0s] Topology ready: n0/10.1.1.1 -> n1/10.1.1.2:8080");
        NS_LOG_UNCOND("[WORKFLOW 0s] Receiver starts at 0s; sender starts at 1s; stop at 20s");
    }

    // Generate a 1 Mbps stream of 1000 packets, each containing 1040 bytes.
    // Allocate the custom sender application as a reference-counted ns-3 object.
    Ptr<TutorialApp> app = CreateObject<TutorialApp>();
    // Reuse the exposed socket and request 1000 sends of 1040 bytes, spaced at a 1 Mbps rate.
    app->Setup(ns3TcpSocket, sinkAddress, 1040, 1000, DataRate("1Mbps"));
    // Aggregate TutorialApp onto sender node 0 so ns-3 manages its lifecycle.
    nodes.Get(0)->AddApplication(app);
    // At t=1 the app binds/connects the socket and immediately offers its first packet to TCP.
    app->SetStartTime(Seconds(1.));
    // At t=20 it cancels any pending send event and closes the socket.
    app->SetStopTime(Seconds(20.));

    if (detailedLog)
    {
        // ssthresh separates exponential slow start from linear congestion avoidance.
        ns3TcpSocket->TraceConnectWithoutContext("SlowStartThreshold",
                                                 MakeCallback(&SsThreshChange));
        // CongState exposes CA_OPEN, CA_DISORDER, CA_RECOVERY, and CA_LOSS transitions.
        ns3TcpSocket->TraceConnectWithoutContext("CongState", MakeCallback(&CongStateChange));
        // State exposes connection setup/teardown, including SYN_SENT and ESTABLISHED.
        ns3TcpSocket->TraceConnectWithoutContext("State", MakeCallback(&ConnectionStateChange));
        // RTT and RTO show how acknowledgment timing drives timeout calculation.
        ns3TcpSocket->TraceConnectWithoutContext("RTT", MakeCallback(&RttChange));
        ns3TcpSocket->TraceConnectWithoutContext("RTO", MakeCallback(&RtoChange));
        // BytesInFlight can be compared with cwnd to see when transmission is window-limited.
        ns3TcpSocket->TraceConnectWithoutContext("BytesInFlight",
                                                 MakeCallback(&BytesInFlightChange));
        // Retransmission marks the exact sequence range TCP sends again after detecting loss.
        ns3TcpSocket->TraceConnectWithoutContext("Retransmission", MakeCallback(&Retransmission));
    }

    // Create sixth.cwnd and bind its stream into the congestion callback.
    // This helper manages an ostream whose lifetime safely spans all later callback invocations.
    AsciiTraceHelper asciiTraceHelper;
    // Open/truncate sixth.cwnd and wrap its stream in a reference-counted ns-3 object.
    Ptr<OutputStreamWrapper> stream = asciiTraceHelper.CreateFileStream("sixth.cwnd");
    // Bind stream as the callback's first argument; TCP supplies old/new cwnd on every change.
    ns3TcpSocket->TraceConnectWithoutContext("CongestionWindow",
                                             MakeBoundCallback(&CwndChange, stream));

    // Create sixth.pcap and bind its writer into the receive-drop callback.
    // Use PcapHelper to construct a correctly formatted binary packet-capture writer.
    PcapHelper pcapHelper;
    // Open/truncate sixth.pcap and retain its wrapper for the complete simulation.
    // DLT_PPP tells packet-analysis tools how to decode point-to-point link records.
    Ptr<PcapFileWrapper> file =
        pcapHelper.CreateFile("sixth.pcap", std::ios::out, PcapHelper::DLT_PPP);
    // Bind file first; PhyRxDrop later supplies each immutable packet rejected by RateErrorModel.
    devices.Get(1)->TraceConnectWithoutContext("PhyRxDrop", MakeBoundCallback(&RxDrop, file));

    // Print nodes, applications, devices, interfaces, channels, and optionally all attributes.
    SimulationDebugHelper::PrintTopology("ns-3 Sixth Tutorial TCP Topology", printAttributes);
    // Summarize the flow and name both persistent trace outputs for the user.
    std::cout << "\nTCP flow: n0/" << interfaces.GetAddress(0) << " -> n1/"
              << interfaces.GetAddress(1) << ":" << sinkPort
              << "\nTrace outputs: sixth.cwnd and sixth.pcap\n";
    if (tracePackets)
    {
        // Add verbose IPv4/TCP send, forwarding, local-delivery, and drop diagnostics.
        SimulationDebugHelper::EnableIpv4PacketFlowTracing();
    }

    // Execute no later than 20 seconds, then clean up simulator resources.
    if (detailedLog)
    {
        NS_LOG_UNCOND("[WORKFLOW 0s] Traces connected; entering Simulator::Run()");
    }

    // Schedule a hard stop so no remaining protocol timer can extend execution beyond 20 s.
    Simulator::Stop(Seconds(20));
    // Process queued events in timestamp order until the global stop event executes.
    Simulator::Run();

    if (detailedLog)
    {
        NS_LOG_UNCOND("[WORKFLOW " << Simulator::Now().GetSeconds()
                                   << "s] Event loop stopped; collecting final counters");
    }

    // Recover PacketSink's concrete API so its total application-byte counter can be read.
    Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApps.Get(0));
    // Print completion time and delivered bytes after all simulation events have finished.
    std::cout << "\nSimulation summary: finished at " << Simulator::Now().GetSeconds()
              << " s, TCP sink received " << sink->GetTotalRx()
              << " bytes; congestion/drop traces written to disk\n";
    // Release simulator-global events and singleton state before process termination.
    Simulator::Destroy();

    // Return success to the operating system.
    return 0;
}
