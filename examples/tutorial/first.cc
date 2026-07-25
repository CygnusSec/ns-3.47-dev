/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Import helpers for installing traffic-generating applications such as UDP Echo.
#include "ns3/applications-module.h"
// Import core ns-3 facilities such as time, logging, command-line parsing, and the simulator.
#include "ns3/core-module.h"
// Import the Internet protocol stack, IPv4 addressing, UDP, and routing support.
#include "ns3/internet-module.h"
// Import fundamental network objects such as nodes, packets, devices, and channels.
#include "ns3/network-module.h"
// Import the point-to-point channel, network device, and its configuration helper.
#include "ns3/point-to-point-module.h"

// The simulation creates this simple two-node network topology:
//
//          IPv4 network 10.1.1.0/24
//     n0 --------------------------- n1
//       point-to-point link: 5 Mbps, 2 ms
//     UDP Echo client             UDP Echo server
//

// Make ns-3 classes available without repeatedly writing the "ns3::" namespace prefix.
using namespace ns3;

// Register a named log component for messages emitted directly by this source file.
NS_LOG_COMPONENT_DEFINE("FirstScriptExample");

// Every C++ executable starts from main().
// argc contains the number of command-line arguments and argv contains their values.
int
main(int argc, char* argv[])
{
    // Create an ns-3 command-line parser and use this source filename in its help text.
    CommandLine cmd(__FILE__);

    // Parse options such as "--help" and any arguments registered with cmd.AddValue().
    cmd.Parse(argc, argv);

    // Set nanoseconds as the smallest representable unit of simulated time.
    Time::SetResolution(Time::NS);

    // Show informational log messages produced by the UDP Echo client application.
    LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);

    // Show informational log messages produced by the UDP Echo server application.
    LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);

    // Create a container that will keep references to the simulated nodes.
    NodeContainer nodes;

    // Allocate two nodes. They are accessible as nodes.Get(0) and nodes.Get(1).
    nodes.Create(2);

    // Create a helper that configures and installs a point-to-point connection.
    PointToPointHelper pointToPoint;

    // Configure each point-to-point network device to transmit at 5 megabits per second.
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("5Mbps"));

    // Configure the channel to deliver a transmitted bit after 2 ms of propagation delay.
    pointToPoint.SetChannelAttribute("Delay", StringValue("2ms"));

    // Create a container for the network devices that the helper will return.
    NetDeviceContainer devices;

    // Install one point-to-point device on each node and connect both devices to one channel.
    devices = pointToPoint.Install(nodes);

    // Create a helper that installs IPv4, ARP, UDP, TCP, ICMP, and routing components.
    InternetStackHelper stack;

    // Install one complete Internet protocol stack on every node in the container.
    stack.Install(nodes);

    // Create a helper that allocates IPv4 addresses from a configured subnet.
    Ipv4AddressHelper address;

    // Select network 10.1.1.0 with a /24 mask; usable host addresses begin at 10.1.1.1.
    address.SetBase("10.1.1.0", "255.255.255.0");

    // Assign one IPv4 address to each device and retain the resulting IPv4 interfaces.
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    // Configure a UDP Echo server factory that listens on UDP port 9.
    UdpEchoServerHelper echoServer(9);

    // Create the server application and install it on node n1 (the second node).
    ApplicationContainer serverApps = echoServer.Install(nodes.Get(1));

    // Schedule the server application to start at simulation time 1 second.
    serverApps.Start(Seconds(1));

    // Schedule the server application to stop at simulation time 10 seconds.
    serverApps.Stop(Seconds(10));

    // Configure a UDP Echo client whose destination is n1's IPv4 address and UDP port 9.
    UdpEchoClientHelper echoClient(interfaces.GetAddress(1), 9);

    // Tell the client to transmit exactly one UDP Echo request packet.
    echoClient.SetAttribute("MaxPackets", UintegerValue(1));

    // Set one second between packets; this matters if MaxPackets is greater than one.
    echoClient.SetAttribute("Interval", TimeValue(Seconds(1)));

    // Set the application payload size of each UDP packet to 1024 bytes.
    echoClient.SetAttribute("PacketSize", UintegerValue(1024));

    // Create the client application and install it on node n0 (the first node).
    ApplicationContainer clientApps = echoClient.Install(nodes.Get(0));

    // Start the client at 2 seconds, after the server has already started.
    clientApps.Start(Seconds(2));

    // Stop the client at simulation time 10 seconds if it has not already completed.
    clientApps.Stop(Seconds(10));

    // Execute scheduled events in timestamp order until no events remain.
    Simulator::Run();

    // Release simulator-owned events and global state so the process exits cleanly.
    Simulator::Destroy();

    // Return zero to report successful execution to the operating system.
    return 0;
}
