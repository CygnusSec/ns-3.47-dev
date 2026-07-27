#
# SPDX-License-Identifier: GPL-2.0-only
#

# Import the generated ns-3 Python bindings and provide a useful failure message.
try:
    from ns import ns
except ModuleNotFoundError:
    raise SystemExit(
        "Error: ns3 Python module not found;"
        " Python bindings may not be enabled"
        " or your PYTHONPATH might not be properly configured"
    )

# The simulation creates a two-node point-to-point network:
# n0 (UDP Echo client) ---- 10.1.1.0/24 ---- n1 (UDP Echo server).

# Enable readable application-level send and receive messages.
ns.LogComponentEnable("UdpEchoClientApplication", ns.LOG_LEVEL_INFO)
ns.LogComponentEnable("UdpEchoServerApplication", ns.LOG_LEVEL_INFO)

# Allocate two simulated computers.
nodes = ns.NodeContainer()
nodes.Create(2)

# Configure and install a 5 Mbps link with 2 ms propagation delay.
pointToPoint = ns.PointToPointHelper()
pointToPoint.SetDeviceAttribute("DataRate", ns.StringValue("5Mbps"))
pointToPoint.SetChannelAttribute("Delay", ns.StringValue("2ms"))

devices = pointToPoint.Install(nodes)

# Install IPv4, routing, UDP, TCP, and related protocols on both nodes.
stack = ns.InternetStackHelper()
stack.Install(nodes)

# Allocate one IPv4 address per point-to-point device.
address = ns.Ipv4AddressHelper()
address.SetBase(ns.Ipv4Address("10.1.1.0"), ns.Ipv4Mask("255.255.255.0"))

# Retain the interfaces so the client can discover the server address.
interfaces = address.Assign(devices)

# Install a UDP Echo server on n1, listening from 1 to 10 seconds.
echoServer = ns.UdpEchoServerHelper(9)

serverApps = echoServer.Install(nodes.Get(1))
serverApps.Start(ns.Seconds(1))
serverApps.Stop(ns.Seconds(10))

# Convert n1's IPv4 address and configure one 1024-byte request.
address = interfaces.GetAddress(1).ConvertTo()
echoClient = ns.UdpEchoClientHelper(address, 9)
echoClient.SetAttribute("MaxPackets", ns.UintegerValue(1))
echoClient.SetAttribute("Interval", ns.TimeValue(ns.Seconds(1)))
echoClient.SetAttribute("PacketSize", ns.UintegerValue(1024))

# Install the client on n0 and start it after the server.
clientApps = echoClient.Install(nodes.Get(0))
clientApps.Start(ns.Seconds(2))
clientApps.Stop(ns.Seconds(10))

# Execute scheduled events and release global simulator state.
ns.Simulator.Run()
ns.Simulator.Destroy()
