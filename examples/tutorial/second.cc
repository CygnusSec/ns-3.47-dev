/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/simulation-debug-helper.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

// This example extends first.cc with a shared CSMA LAN and IP routing:
//
//       10.1.1.0
// n0 -------------- n1   n2   n3   n4
//    point-to-point  |    |    |    |
//                    ================
//                      LAN 10.1.2.0

using namespace ns3;

// Register the log component associated with this executable.
NS_LOG_COMPONENT_DEFINE("SecondScriptExample");

// Construct, run, and destroy the complete simulation.
int
main(int argc, char* argv[])
{
    // Enable UDP Echo informational logging unless the user disables it.
    bool verbose = true;

    // Create three extra LAN nodes in addition to the point-to-point gateway.
    uint32_t nCsma = 3;

    // Zero selects the final CSMA node; --serverNode can select n2, n3, and so on.
    uint32_t serverNode = 0;

    // Print every node's routing table at 0.5 seconds by default.
    bool printRoutes = true;

    // Print every IPv4 send, forward, and local-delivery action by default.
    bool tracePackets = true;

    // Register user-configurable command-line arguments.
    CommandLine cmd(__FILE__);
    cmd.AddValue("nCsma", "Number of \"extra\" CSMA nodes/devices", nCsma);
    cmd.AddValue("serverNode",
                 "UDP Echo server node number; 0 selects the final CSMA node",
                 serverNode);
    cmd.AddValue("verbose", "Tell echo applications to log if true", verbose);
    cmd.AddValue("printRoutes", "Print all IPv4 routing tables if true", printRoutes);
    cmd.AddValue("tracePackets", "Print IPv4 packet forwarding actions if true", tracePackets);

    // Apply values supplied through arguments such as --nCsma=5.
    cmd.Parse(argc, argv);

    // Enable application logs only when requested.
    if (verbose)
    {
        LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
        LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);
    }

    // Preserve at least one extra CSMA node so the server index remains valid.
    nCsma = nCsma == 0 ? 1 : nCsma;

    // csmaNodes index zero is global node n1, so global node n2 maps to index one.
    if (serverNode == 0)
    {
        serverNode = nCsma + 1;
    }
    NS_ABORT_MSG_IF(serverNode < 2 || serverNode > nCsma + 1,
                    "serverNode must be between 2 and " << nCsma + 1);
    const uint32_t serverCsmaIndex = serverNode - 1;

    // Create the two endpoints of the point-to-point link.
    NodeContainer p2pNodes;
    p2pNodes.Create(2);

    // Build the LAN node set, reusing p2p node 1 as its router/gateway.
    NodeContainer csmaNodes;
    csmaNodes.Add(p2pNodes.Get(1));
    csmaNodes.Create(nCsma);

    // Configure the point-to-point link as 5 Mbps with 2 ms propagation delay.
    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("2ms"));

    // Install and retain the two point-to-point network devices.
    NetDeviceContainer p2pDevices;
    p2pDevices = pointToPoint.Install(p2pNodes);

    // Configure a 100 Mbps shared CSMA channel with a 6560 ns delay.
    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", StringValue("100Mbps"));
    csma.SetChannelAttribute("Delay", TimeValue(NanoSeconds(6560)));

    // Attach one CSMA network device to every LAN node.
    NetDeviceContainer csmaDevices;
    csmaDevices = csma.Install(csmaNodes);

    // Install an Internet stack once on every unique node.
    InternetStackHelper stack;
    // p2p node 0 is not part of csmaNodes, so install it separately.
    stack.Install(p2pNodes.Get(0));
    // This includes gateway p2p node 1 plus all extra LAN nodes.
    stack.Install(csmaNodes);

    // Assign 10.1.1.0/24 to the point-to-point segment.
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer p2pInterfaces;
    p2pInterfaces = address.Assign(p2pDevices);

    // Assign a different subnet, 10.1.2.0/24, to the CSMA LAN.
    address.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer csmaInterfaces;
    csmaInterfaces = address.Assign(csmaDevices);

    // Create a UDP Echo server listening on port 9.
    UdpEchoServerHelper echoServer(9);

    // Install the server on the selected extra CSMA node.
    ApplicationContainer serverApps = echoServer.Install(csmaNodes.Get(serverCsmaIndex));
    // Start the server before the client and stop it at 10 seconds.
    serverApps.Start(Seconds(1));
    serverApps.Stop(Seconds(10));

    // Send Echo requests to the server's CSMA address and port.
    UdpEchoClientHelper echoClient(csmaInterfaces.GetAddress(serverCsmaIndex), 9);
    // Send one 1024-byte packet, with a one-second interval if more are requested.
    echoClient.SetAttribute("MaxPackets", UintegerValue(1));
    echoClient.SetAttribute("Interval", TimeValue(Seconds(1)));
    echoClient.SetAttribute("PacketSize", UintegerValue(1024));

    // Install the client on the far point-to-point endpoint.
    ApplicationContainer clientApps = echoClient.Install(p2pNodes.Get(0));
    clientApps.Start(Seconds(2));
    clientApps.Stop(Seconds(10));

    // Calculate routes between the point-to-point and CSMA subnets.
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Print a readable description after every device and address is known.
    // Discover all nodes, devices, channels, applications, and IP addresses.
    SimulationDebugHelper::PrintTopology("ns-3 Second Tutorial Topology");

    // Print configuration that belongs specifically to this UDP Echo example.
    std::cout << "\nApplication flow\n"
              << "  Protocol     : UDP Echo\n"
              << "  Source       : n0/" << p2pInterfaces.GetAddress(0) << "\n"
              << "  Destination  : n" << serverNode << "/"
              << csmaInterfaces.GetAddress(serverCsmaIndex) << ":9\n"
              << "  Expected path: n0 -> n1 -> n" << serverNode << " -> n1 -> n0\n";

    // Schedule a detailed routing-table dump before application traffic begins.
    if (printRoutes)
    {
        SimulationDebugHelper::PrintIpv4RoutingTablesAt(Seconds(0.5));
    }

    // Connect after configuration but before Simulator::Run() processes any traffic.
    if (tracePackets)
    {
        SimulationDebugHelper::EnableIpv4PacketFlowTracing();
    }

    // Capture every point-to-point device into second-*.pcap files.
    pointToPoint.EnablePcapAll("second");
    // Capture the selected CSMA device; true enables promiscuous capture.
    csma.EnablePcap("second", csmaDevices.Get(1), true);

    // Process all scheduled events, then free global simulator state.
    Simulator::Run();

    // Simulator::Now() is 10 seconds because application stop events remain in the queue.
    std::cout << "\n================ Simulation Summary =================\n"
              << "Finished at       : " << Simulator::Now().GetSeconds() << " s\n"
              << "UDP Echo exchange : completed\n"
              << "Request path      : n0 SEND -> n1 FORWARD -> n" << serverNode
              << " DELIVER\n"
              << "Reply path        : n" << serverNode
              << " SEND -> n1 FORWARD -> n0 DELIVER\n"
              << "Routing tables    : " << (printRoutes ? "printed" : "disabled") << "\n"
              << "IPv4 packet trace : " << (tracePackets ? "printed above" : "disabled") << "\n"
              << "PCAP traces       : written with prefix second\n"
              << "=====================================================\n";

    Simulator::Destroy();
    return 0;
}
