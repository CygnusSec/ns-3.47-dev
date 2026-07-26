/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Import the core module, which provides logging and the basic ns-3 runtime.
#include "ns3/core-module.h"

// Avoid writing the ns3:: prefix before every ns-3 class or function.
using namespace ns3;

// Define the log component associated with this small example.
NS_LOG_COMPONENT_DEFINE("HelloSimulator");

// Program entry point; this example does not need its command-line arguments.
int
main(int argc, char* argv[])
{
    // Print unconditionally, even when normal ns-3 log components are disabled.
    NS_LOG_UNCOND("Hello Simulator");
    NS_LOG_UNCOND("Runtime summary: core module loaded; no nodes, devices, channels, or events");

    // Report successful completion to the operating system.
    return 0;
}
