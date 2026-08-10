/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/netanim-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-layout-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/simulation-debug-helper.h"


// Network topology (default)
//
//        n0 - n1                .
//         |    |                .
//         |    |                .
//        n2 - n3                .
//

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Grid");

int
main(int argc, char* argv[])
{
    bool printAttributes = true;
    // Default number of nodes in the grid.  Overridable by command line argument.
    uint32_t nRows = 2;
    uint32_t nColumns = 2;
    uint32_t nDiags = 2;

    // Use the source filename in --help output so users can identify the owning example.
    CommandLine cmd(__FILE__);
    cmd.AddValue("printAttributes", "Print the attributes of each node", printAttributes);
    cmd.AddValue("nRows", "Number of rows in the grid", nRows);
    cmd.AddValue("nColumns", "Number of columns in the grid", nColumns);
    cmd.AddValue("nDiags", "Number of diagonals in the grid", nDiags);
    // Parse arguments before creating model objects so every option affects initial configuration.
    cmd.Parse(argc, argv);

    //
    // Set up some default values for the simulation.
    //
    Config::SetDefault("ns3::OnOffApplication::PacketSize", UintegerValue(137));

    // ??? try and stick 15kb/s into the data rate
    Config::SetDefault("ns3::OnOffApplication::DataRate", StringValue("14kb/s"));

    NS_LOG_INFO("Build grid topology.");
    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("2ms"));
    PointToPointGridHelper grid(nRows, nColumns, nDiags, pointToPoint);

    NS_LOG_INFO("Install internet stack on all nodes.");
    InternetStackHelper internet;
    grid.InstallStack(internet);

    NS_LOG_INFO("Assign IP Addresses.");
    grid.AssignIpv4Addresses(Ipv4AddressHelper("10.1.0.0", "255.255.255.0"), Ipv4AddressHelper("10.2.0.0", "255.255.255.0"), Ipv4AddressHelper("10.3.0.0", "255.255.255.0"));
    SimulationDebugHelper::PrintTopology("ns-3 grid Topology", printAttributes);
}