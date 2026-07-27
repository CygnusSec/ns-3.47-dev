# Understanding the ns-3 Source Code and Development Workflow

This tutorial provides a structured path for understanding the ns-3.47 source
tree, its runtime architecture, and its development workflow. It is intended
for developers who want to do more than run examples: the goal is to understand
how a simulation is assembled, how packets move through the simulator, how
events are executed, and where new code should be added.

Reading the repository sequentially is not practical. It contains thousands of
source files and many independent protocol models. A more effective approach is
to understand the architectural layers, trace one small simulation from
`main()` down to the event scheduler, and then study the module relevant to
your research.

## 1. Architectural Overview

The main ns-3 layers can be viewed as follows:

```text
Simulation program
        |
        v
Helpers
Create and configure nodes, devices, stacks, and applications
        |
        v
Applications
Generate and consume network traffic
        |
        v
Internet
TCP, UDP, IPv4, IPv6, and routing
        |
        v
Network
Node, Packet, Socket, NetDevice, and Channel abstractions
        |
        v
Link and PHY models
Wi-Fi, LTE, CSMA, Point-to-Point, LR-WPAN, and others
        |
        v
Core
Simulator, event scheduler, time, objects, attributes, and callbacks
```

The most important architectural fact is that ns-3 is a discrete-event
simulator. It does not normally execute a network in real wall-clock time.
Instead, it stores timestamped events and advances simulated time directly to
the next event.

Most ns-3 simulations are single-threaded. This makes event ordering and seeded
experiments deterministic when the model itself does not depend on external
real-time state.

## 2. Repository Structure

The major repository directories are:

```text
ns-3.47-dev/
├── src/             Official simulator modules
├── scratch/         User simulations and experiments
├── contrib/         Custom and third-party modules
├── examples/        General examples and tutorials
├── bindings/        Python-binding support
├── doc/             Installation, manual, tutorial, and model documentation
├── utils/           Testing, analysis, formatting, and maintenance tools
├── build-support/   CMake macros and dependency detection
├── CMakeLists.txt   Top-level CMake configuration
├── ns3              Python wrapper around CMake
└── test.py          Test-suite runner
```

For normal research simulations:

- Put standalone experiments in `scratch/`.
- Put reusable custom models in `contrib/`.
- Modify `src/` only when changing an official ns-3 module.

## 3. Standard Module Structure

An ns-3 module normally follows this structure:

```text
src/<module>/
├── model/          Protocol or model implementation
├── helper/         High-level configuration APIs
├── test/           Unit, system, and regression tests
├── examples/       Module-specific simulations
├── doc/            Design and usage documentation
└── CMakeLists.txt  Sources, headers, tests, and dependencies
```

The distinction between `model/` and `helper/` is fundamental:

- `model/` answers: "How does this protocol or system behave?"
- `helper/` answers: "How does a simulation create and configure this model?"

For example:

```text
src/point-to-point/
├── helper/
│   ├── point-to-point-helper.cc
│   └── point-to-point-helper.h
├── model/
│   ├── point-to-point-channel.cc
│   ├── point-to-point-net-device.cc
│   └── ppp-header.cc
├── test/
├── examples/
└── CMakeLists.txt
```

## 4. Core Simulator Module

The `src/core` module is the simulator kernel. Important files include:

- `simulator.cc`: public static simulator API.
- `default-simulator-impl.cc`: default event-loop implementation.
- `event-id.cc` and `event-impl.cc`: event representation.
- `time.cc`: simulated time and time units.
- `scheduler.cc`: scheduler interface.
- `heap-scheduler.cc`, `map-scheduler.cc`, and
  `calendar-scheduler.cc`: event-queue implementations.
- `object.cc` and `object-base.cc`: ns-3 object system.
- `type-id.cc`: runtime type registration.
- `attribute.cc`: configurable attribute system.
- `callback.cc`: type-safe callbacks.
- `config.cc`: global configuration and trace connection.
- `random-variable-stream.cc`: repeatable random streams.
- `log.cc`: component-based logging.

The central concepts to learn are:

```text
Simulator
Event
Time
Object
Ptr<T>
TypeId
Attribute
Callback
TraceSource
RandomVariableStream
```

### 4.1 Event loop

A simplified representation of the default event loop is:

```cpp
while (!eventQueue.empty())
{
    Event event = eventQueue.RemoveNext();
    currentTime = event.time;
    event.Execute();
}
```

The real implementation also handles event IDs, cancellation, stop conditions,
context, scheduler selection, and simulator destruction.

### 4.2 Scheduling an event

A model can schedule a future callback:

```cpp
Simulator::Schedule(
    MilliSeconds(2),
    &Receiver::Receive,
    receiver,
    packet);
```

This does not wait for two real milliseconds. It inserts an event with a
timestamp two simulated milliseconds after the current simulation time.

## 5. Network Module

The `src/network` module provides technology-independent networking
abstractions:

- `Node`: a simulated host, router, base station, or other network entity.
- `Packet`: data carried through the simulator.
- `Application`: a traffic producer or consumer installed on a node.
- `Socket`: application-facing transport interface.
- `NetDevice`: network-interface abstraction.
- `Channel`: communication medium connecting devices.
- `Header` and `Trailer`: serializable packet protocol data.
- `Tag`: simulator metadata that does not need to be serialized as packet data.
- Queue and queue-limit abstractions.
- Error models.
- PCAP and ASCII tracing support.

A simplified node composition is:

```text
Node
├── Application
├── Protocol stack
└── NetDevice ─── Channel ─── NetDevice
```

Objects such as protocol stacks are frequently aggregated onto a `Node`.
Applications and devices are maintained in node-owned collections.

## 6. Internet and Application Modules

### 6.1 Internet module

The `src/internet` module contains:

- IPv4 and IPv6
- ARP and ICMP
- UDP
- TCP
- Static and global routing
- Routing protocol interfaces
- Interfaces and endpoints
- Raw sockets

TCP congestion-control implementations include:

```text
tcp-bbr.cc
tcp-cubic.cc
tcp-dctcp.cc
tcp-linux-reno.cc
tcp-vegas.cc
tcp-westwood-plus.cc
```

TCP is a large subsystem. It is easier to study UDP, IPv4, and routing before
following the TCP state machine.

### 6.2 Applications module

The `src/applications` module includes traffic-generating and traffic-consuming
models such as:

- UDP Echo client and server
- PacketSink
- OnOffApplication
- BulkSendApplication
- UDP client and server
- HTTP traffic
- Video, VoIP, virtual-desktop, and gaming traffic

An application does not normally transmit directly through a channel. It uses
a `Socket`, and the transport, network, and device layers process the packet.

## 7. Link, Wireless, and Supporting Modules

### 7.1 Device and PHY modules

- `point-to-point`: a link connecting exactly two endpoints.
- `csma`: a shared-medium Ethernet-like model.
- `wifi`: IEEE 802.11 PHY, MAC, QoS, rate control, 802.11ax, and 802.11be.
- `lte`: LTE radio access and EPC models.
- `lr-wpan`: IEEE 802.15.4.
- `zigbee`: Zigbee layers over LR-WPAN.
- `sixlowpan`: IPv6 adaptation for low-power networks.
- `uan`: underwater acoustic networking.

### 7.2 Supporting modules

- `mobility`: node position and motion.
- `propagation`: propagation-loss and channel-condition models.
- `spectrum`: frequency- and spectrum-aware channel models.
- `antenna`: antenna radiation models and arrays.
- `buildings`: indoor/outdoor geometry and building-aware loss.
- `energy`: energy sources, consumption, and harvesting.
- `flow-monitor`: per-flow throughput, delay, and loss statistics.
- `traffic-control`: queue disciplines such as FQ-CoDel, CoDel, PIE, and RED.
- `netanim`: XML trace generation for NetAnim.

## 8. Following the First Tutorial Simulation

The recommended starting point is:

```text
examples/tutorial/first.cc
```

It creates two nodes connected by a point-to-point link and exchanges one UDP
Echo request and response.

### 8.1 Include module interfaces

```cpp
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
```

Each `*-module.h` header collects the public headers exported by that module.
This is convenient for simulations, although individual headers are often
preferred inside module implementations.

### 8.2 Parse command-line parameters

```cpp
CommandLine cmd(__FILE__);
cmd.Parse(argc, argv);
```

This allows simulation attributes and explicitly registered values to be
provided when launching the program.

### 8.3 Create nodes

```cpp
NodeContainer nodes;
nodes.Create(2);
```

A simplified internal path is:

```text
NodeContainer::Create()
    └── CreateObject<Node>()
            ├── Object construction
            ├── TypeId lookup
            └── NodeList registration
```

### 8.4 Create the point-to-point link

```cpp
PointToPointHelper pointToPoint;

pointToPoint.SetDeviceAttribute(
    "DataRate",
    StringValue("5Mbps"));

pointToPoint.SetChannelAttribute(
    "Delay",
    StringValue("2ms"));

NetDeviceContainer devices =
    pointToPoint.Install(nodes);
```

The helper performs work similar to:

```text
PointToPointHelper::Install()
    ├── Create a PointToPointChannel
    ├── Create two PointToPointNetDevices
    ├── Add one NetDevice to each Node
    └── Attach both NetDevices to the Channel
```

### 8.5 Install the Internet stack

```cpp
InternetStackHelper stack;
stack.Install(nodes);
```

The helper aggregates protocol objects onto each node. Depending on the
configuration, these include objects such as:

```text
Node
├── Ipv4L3Protocol
├── Ipv6L3Protocol
├── ArpL3Protocol
├── Icmpv4L4Protocol
├── UdpL4Protocol
├── TcpL4Protocol
└── RoutingProtocol
```

### 8.6 Assign IP addresses

```cpp
Ipv4AddressHelper address;
address.SetBase("10.1.1.0", "255.255.255.0");

Ipv4InterfaceContainer interfaces =
    address.Assign(devices);
```

Each `NetDevice` is associated with an IPv4 interface and receives an address
from the configured subnet.

### 8.7 Install applications

```cpp
UdpEchoServerHelper server(9);
ApplicationContainer serverApps =
    server.Install(nodes.Get(1));

serverApps.Start(Seconds(1));
serverApps.Stop(Seconds(10));
```

`Start()` and `Stop()` do not immediately run or terminate the application.
They schedule events:

```text
time = 1 s  → Application::StartApplication()
time = 10 s → Application::StopApplication()
```

The client is installed in the same way, with attributes controlling packet
count, packet size, and transmission interval.

### 8.8 Run the simulation

```cpp
Simulator::Run();
Simulator::Destroy();
```

`Run()` executes timestamped events until the event queue is empty or a stop
condition is reached. `Destroy()` executes destroy-time events and releases
global simulator state.

## 9. End-to-End UDP Packet Flow

The request packet in the first tutorial follows this conceptual path:

```text
UdpEchoClientApplication
        |
        v
Socket::Send()
        |
        v
UdpSocketImpl
        |
        v
UdpL4Protocol
Add UDP header
        |
        v
Ipv4L3Protocol
Add IPv4 header and select a route
        |
        v
TrafficControlLayer
        |
        v
PointToPointNetDevice
        |
        v
PointToPointChannel
Schedule a receive event after the propagation delay
        |
        v
Destination PointToPointNetDevice
        |
        v
Ipv4L3Protocol
        |
        v
UdpL4Protocol
        |
        v
UdpEchoServerApplication
```

The server creates a reply, which travels through the same layers in the
opposite direction.

An approximate event timeline is:

```text
1.000000 s  Start server
2.000000 s  Start client
2.000000 s  Client sends request
2.003686 s  Server receives request
2.003686 s  Server sends reply
2.007372 s  Client receives reply
10.00000 s  Stop applications
```

This packet path and event timeline are the most useful starting points for
source-level debugging.

## 10. Object, TypeId, and Attribute Workflow

Most configurable ns-3 classes derive from `Object` and register a `TypeId`.

```cpp
class Example : public Object
{
  public:
    static TypeId GetTypeId();
};
```

An attribute can be registered as follows:

```cpp
TypeId
Example::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::Example")
            .SetParent<Object>()
            .AddConstructor<Example>()
            .AddAttribute(
                "Delay",
                "Propagation delay",
                TimeValue(MilliSeconds(1)),
                MakeTimeAccessor(&Example::m_delay),
                MakeTimeChecker());
    return tid;
}
```

The simulation can then configure it by name:

```cpp
helper.SetAttribute(
    "Delay",
    TimeValue(MilliSeconds(5)));
```

The conceptual workflow is:

```text
Attribute name: "Delay"
        |
        v
TypeId finds AttributeInformation
        |
        v
AttributeChecker validates the value
        |
        v
AttributeAccessor updates m_delay
```

This mechanism allows configuration through helpers, direct object calls,
`Config`, command-line options, or ConfigStore without requiring a dedicated
setter for every value.

## 11. Tracing Workflow

Tracing separates model behavior from data collection. A model can expose a
trace source:

```cpp
TracedCallback<Ptr<const Packet>> m_txTrace;
```

When a transmission occurs, the model invokes it:

```cpp
m_txTrace(packet);
```

A simulation connects a callback:

```cpp
Config::Connect(
    "/NodeList/*/DeviceList/*/MacTx",
    MakeCallback(&TracePacket));
```

Trace sources can expose:

- Packet transmission, reception, and drops
- TCP congestion-window changes
- Queue length and queue delay
- PHY and MAC state changes
- Signal strength and interference
- End-to-end delay
- Throughput
- Energy consumption

Common output mechanisms include:

- Component logging
- ASCII traces
- PCAP packet captures
- FlowMonitor XML
- NetAnim XML
- Custom CSV data

## 12. Randomness and Reproducibility

Simulation randomness should use ns-3 random-variable streams rather than
unmanaged standard-library randomness.

The important controls are:

- Seed: identifies a family of random streams.
- Run number: selects an independent substream for a repeated experiment.
- Stream assignment: gives models predictable, non-overlapping random streams.

A well-designed experiment explicitly controls these values so that a result
can be reproduced and multiple runs can be compared statistically.

## 13. Docker Development Workflow

The repository includes a persistent Docker Compose development environment.
The host requires Docker Desktop but does not need a native compiler or ns-3
dependencies.

### 13.1 Build and start the container

From the repository root:

```bash
docker compose build
docker compose up -d
```

### 13.2 Check its status

```bash
docker compose ps
```

### 13.3 Enter the running container

```bash
docker compose exec ns3 bash
```

The repository is available inside the container at:

```text
/workspace/ns-3
```

### 13.4 Build and run the first tutorial

Inside the container:

```bash
./ns3 build first -j 2
./ns3 run first --no-build
```

Using an explicit build target avoids compiling every enabled example and test.
The `--no-build` option prevents the subsequent run command from invoking the
default full build.

### 13.5 Build and run a scratch simulation

Create a file on the host:

```text
scratch/my-simulation.cc
```

Because the repository is bind-mounted, it immediately appears inside the
container. Build and run it:

```bash
./ns3 build my-simulation -j 2
./ns3 run my-simulation --no-build
```

After changing the source:

```bash
./ns3 build my-simulation -j 2
./ns3 run my-simulation --no-build
```

Ninja recompiles only the affected files and targets.

### 13.6 Run tests

```bash
./test.py
./test.py -s core
./test.py -s internet
./test.py -s wifi
```

The full suite may require all test targets to be compiled first.

### 13.7 Leave and re-enter the container

Leave the shell:

```bash
exit
```

The persistent container continues running. Re-enter it later:

```bash
docker compose exec ns3 bash
```

### 13.8 Container lifecycle

```bash
# Stop without removing the container
docker compose stop

# Start it again
docker compose start

# Stop and remove the container and network
docker compose down
```

The source remains in the host repository. Linux build output, the CMake cache,
and compiler cache data are stored in Compose named volumes so they cannot be
mixed with native macOS or Windows build artifacts.

## 14. Standalone Simulation Workflow

A standalone research program belongs in `scratch/`:

```text
scratch/my-experiment.cc
```

For a multi-file experiment:

```text
scratch/my-experiment/
├── main.cc
├── scenario.cc
└── scenario.h
```

If the directory does not contain its own `CMakeLists.txt`, the scratch build
logic locates the source containing `main()` and builds all `.cc` files in that
directory as one executable.

Use a local `CMakeLists.txt` when the scratch program requires special external
libraries or custom target configuration.

## 15. Reusable Module Development Workflow

Code that should be reusable across simulations belongs in a custom module,
normally under `contrib/`:

```text
contrib/my-module/
├── CMakeLists.txt
├── model/
│   ├── my-model.cc
│   └── my-model.h
├── helper/
│   ├── my-helper.cc
│   └── my-helper.h
├── test/
│   └── my-model-test-suite.cc
├── examples/
│   └── my-model-example.cc
└── doc/
    └── my-model.rst
```

A recommended development sequence is:

```text
Define the model requirements
        |
        v
Design the public API and attributes
        |
        v
Implement model/
        |
        v
Implement helper/
        |
        v
Write unit and system tests
        |
        v
Write a minimal example
        |
        v
Run controlled simulations
        |
        v
Collect traces and statistics
        |
        v
Validate results against expected behavior
```

### 15.1 Model implementation

The model should contain the protocol state, timers, packet handling, state
transitions, and trace sources. Keep simulation-scenario decisions out of the
model when possible.

### 15.2 Helper implementation

The helper should create and connect model objects using `ObjectFactory`,
containers, and attribute configuration. A helper should simplify scenario
construction without duplicating protocol behavior.

### 15.3 Tests

Tests should cover:

- Attribute defaults and validation
- State transitions
- Packet serialization and deserialization
- Boundary values
- Timing behavior
- Deterministic regression scenarios
- Known analytical results where available

### 15.4 Examples

An example should demonstrate one clear use case with a small topology and a
short runtime. It should not be the only validation of the model.

## 16. Debugging Source-Level Workflows

Debugging an ns-3 simulation is different from debugging a conventional
network service. A wrong result may be caused by scenario configuration,
event ordering, protocol state, a packet-processing decision, or a genuine
memory error. Use the least intrusive tool that can answer the current
question:

```text
Unexpected result
    → logging and trace sources

Wrong event order or protocol state
    → logging, focused breakpoints, and watchpoints

Crash, assertion, or invalid memory access
    → GDB, sanitizers, or Valgrind

Test failure
    → test-runner with --assert-on-failure under GDB
```

Before debugging, reduce the topology, simulation duration, and number of
packets. A two-node, one-flow reproduction is much easier to inspect than a
large Wi-Fi or LTE scenario.

### 16.1 Confirm the debug build

The Docker environment configures ns-3 with:

```text
--build-profile=debug
```

Confirm the active profile inside the container:

```bash
./ns3 show profile
```

Build only the target being investigated:

```bash
./ns3 build first -j 2
```

Run the already-built executable without invoking the default build:

```bash
./ns3 run first --no-build
```

The debug profile enables runtime assertions, logging, and debug symbols.
Optimized builds may inline functions or report that variables have been
optimized out, making source-level inspection more difficult.

If the existing build was configured with another profile, reconfigure it:

```bash
./ns3 configure \
    --build-profile=debug \
    --enable-examples \
    --enable-tests \
    --disable-werror \
    -G Ninja

./ns3 build first -j 2
```

### 16.2 Use component logging

Programs can enable log components:

```cpp
LogComponentEnable(
    "UdpEchoClientApplication",
    LOG_LEVEL_INFO);
```

Environment-based logging can also be used:

```bash
NS_LOG="UdpEchoClientApplication=level_all|prefix_time|prefix_func" \
    ./ns3 run first --no-build
```

Enable multiple components by separating them with a colon:

```bash
NS_LOG="UdpEchoClientApplication=level_all|prefix_time|prefix_func:UdpEchoServerApplication=level_all|prefix_time|prefix_func" \
    ./ns3 run first --no-build
```

Useful prefixes include:

```text
prefix_time     Simulated time
prefix_func     Function name
prefix_node     Node context
prefix_level    Log severity
```

A custom simulation or model defines a component:

```cpp
NS_LOG_COMPONENT_DEFINE("MySimulation");
```

It can then produce messages:

```cpp
NS_LOG_FUNCTION(this << packet);
NS_LOG_DEBUG("State changed from " << oldState << " to " << newState);
NS_LOG_INFO("Sending " << packet->GetSize() << " bytes");
NS_LOG_WARN("Packet was dropped");
NS_LOG_ERROR("Invalid configuration");
```

Enable that component without recompiling:

```bash
NS_LOG="MySimulation=level_all|prefix_time|prefix_func" \
    ./ns3 run my-simulation --no-build
```

Do not begin by enabling all Wi-Fi or LTE components. The output can become
too large to follow and may significantly slow the simulation. Start with the
application or protocol component nearest to the unexpected behavior, then
add one adjacent layer at a time.

Logging is useful for execution flow and state transitions. Trace sources are
usually better for structured research data.

### 16.3 Use trace sources for logical errors

GDB is not always the best tool for incorrect throughput, delay, packet loss,
or congestion-window results. A trace callback records the relevant values
without stopping the simulation.

Example callback:

```cpp
static void
CongestionWindowChanged(uint32_t oldCwnd, uint32_t newCwnd)
{
    std::cout << Simulator::Now().GetSeconds()
              << "," << oldCwnd
              << "," << newCwnd
              << std::endl;
}
```

Example connection:

```cpp
Config::ConnectWithoutContext(
    "/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow",
    MakeCallback(&CongestionWindowChanged));
```

For an unknown trace path, inspect the model's `GetTypeId()` implementation and
search for `.AddTraceSource(...)`:

```bash
rg "AddTraceSource" src/internet
rg "CongestionWindow" src/internet
```

Useful trace targets include:

- Application transmit and receive events
- Packet drops
- Queue size and queue delay
- TCP congestion window
- Routing changes
- PHY state
- RSSI and SNR
- Energy consumption

When possible, write trace output as CSV with simulated time in the first
column. This makes event ordering and comparisons between runs explicit.

### 16.4 Prepare the Docker container for GDB

The repository development image already uses a debug build and installs GDB,
gdbserver, and Valgrind. The relevant package list in `Dockerfile` includes:

```dockerfile
RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        gdb \
        gdbserver \
        valgrind \
        ...
```

The `ns3` service in `compose.yaml` grants the development container
permission to trace its own processes:

```yaml
services:
  ns3:
    cap_add:
      - SYS_PTRACE
    security_opt:
      - seccomp:unconfined
```

These permissions should be used only for the local development container.
They are unnecessary for normal simulation execution and should not be added
to a production-style runtime container.

Recreate the environment after changing the image or Compose configuration:

```bash
docker compose down
docker compose build
docker compose up -d
docker compose exec ns3 bash
```

Verify the debugger:

```bash
gdb --version
gdbserver --version
```

### 16.5 Start GDB through the ns-3 wrapper

Build the target before entering the debugger:

```bash
./ns3 build first -j 2
./ns3 run first --no-build --gdb
```

At the GDB prompt:

```gdb
break main
run
```

Common execution commands:

```gdb
next                 # Execute the next source line without entering a call
step                 # Enter the function called on the current line
finish               # Run until the current function returns
continue             # Continue to the next breakpoint
until                 # Continue until a later line in the current frame
```

Inspection commands:

```gdb
list
info args
info locals
print variable
print packet->GetSize()
ptype variable
backtrace
backtrace full
```

Stack navigation:

```gdb
frame 0
frame 3
up
down
```

Breakpoint management:

```gdb
info breakpoints
disable 2
enable 2
delete 2
clear functionName
```

Exit GDB:

```gdb
quit
```

The wrapper sets library search paths for the built ns-3 shared libraries.
When launching an executable directly with GDB, equivalent library-path
environment variables may need to be configured manually.

### 16.6 Set useful breakpoints

Break at a source line:

```gdb
break examples/tutorial/first.cc:35
```

Break at a function:

```gdb
break ns3::Ipv4L3Protocol::Send
break ns3::PointToPointNetDevice::Send
break ns3::PointToPointNetDevice::Receive
```

If GDB cannot find the function, confirm its exact spelling:

```bash
rg "PointToPointNetDevice::Send" src
rg "UdpEchoClient::" src/applications
```

Set a temporary breakpoint that removes itself after the first hit:

```gdb
tbreak ns3::Ipv4L3Protocol::Send
```

Use a conditional breakpoint to reduce noise:

```gdb
break ns3::PointToPointNetDevice::Send
condition 1 packet->GetSize() == 1024
```

Ignore the first occurrences:

```gdb
ignore 1 10
continue
```

Print information automatically whenever a breakpoint is hit:

```gdb
commands 1
silent
printf "time=%f size=%u\n", ns3::Simulator::Now().GetSeconds(), packet->GetSize()
backtrace 4
continue
end
```

Automatic command blocks are useful when a function is called frequently and
stopping manually would be impractical.

### 16.7 Follow the first tutorial packet

For the UDP Echo tutorial, use a focused sequence of breakpoints. Exact class
names should be confirmed with `rg` because public names may differ from the
implementation class:

```gdb
break ns3::UdpEchoClient::Send
break ns3::UdpSocketImpl::Send
break ns3::UdpL4Protocol::Send
break ns3::Ipv4L3Protocol::Send
break ns3::PointToPointNetDevice::Send
break ns3::PointToPointNetDevice::Receive
break ns3::UdpEchoServer::HandleRead
run
```

At every stop:

```gdb
backtrace
info args
info locals
continue
```

The expected path is:

```text
UdpEchoClient
    → Socket
    → UDP
    → IPv4
    → TrafficControl
    → PointToPointNetDevice
    → PointToPointChannel
    → Destination NetDevice
    → IPv4
    → UDP
    → UdpEchoServer
```

Follow one packet through this path before attempting to debug a complex
wireless scenario.

### 16.8 Debug the event scheduler

Potential scheduler breakpoints include:

```gdb
break ns3::DefaultSimulatorImpl::Schedule
break ns3::DefaultSimulatorImpl::ProcessOneEvent
break ns3::Simulator::Run
```

Scheduling functions are called extremely often. Prefer a conditional
breakpoint, a temporary breakpoint, or a breakpoint on the event callback of
interest.

Useful questions at an event breakpoint are:

- What is the current simulated time?
- Which function scheduled this event?
- What object is the callback targeting?
- Does the target object still exist?
- Did another event with the same timestamp execute first?

Print simulated time from model code:

```cpp
NS_LOG_DEBUG("Now=" << Simulator::Now());
```

When multiple events have the same timestamp, their insertion order and event
UID may affect execution order. Never infer ordering only from rounded
human-readable timestamps.

### 16.9 Debug crashes, assertions, and fatal errors

Start the program under GDB:

```bash
./ns3 run my-simulation --no-build --gdb
```

Configure GDB to stop on common fatal signals:

```gdb
catch signal SIGSEGV
catch signal SIGABRT
run
```

After a crash:

```gdb
backtrace full
frame 0
info args
info locals
list
```

For multi-threaded or MPI-related code:

```gdb
info threads
thread apply all backtrace
```

Common ns-3 crash causes include:

- Dereferencing an empty `Ptr<T>`
- Using an index greater than or equal to `Container::GetN()`
- Installing an application before its required protocol or device
- Starting traffic before interfaces or routes are configured
- Connecting a callback with an incorrect signature
- Scheduling a callback that references an object with an invalid lifetime
- Using the wrong attribute name or value type
- Assuming an object exists at a fixed `NodeList` or `DeviceList` index

For an assertion or fatal error, the first application or model frame above
the assertion implementation is often more useful than frame zero.

### 16.10 Debug a test suite

`test.py` is convenient for running tests but is not the best entry point for
interactive debugging. Use the C++ `test-runner` executable.

Build it:

```bash
./ns3 build test-runner -j 2
```

List suites:

```bash
./ns3 run "test-runner --list" --no-build
```

Run one suite normally:

```bash
./ns3 run "test-runner --suite=core" --no-build
```

Enable logging for one suite:

```bash
NS_LOG="Packet=level_all|prefix_time" \
    ./ns3 run "test-runner --suite=pcap-file" --no-build
```

Debug a suite with GDB:

```bash
./ns3 run \
    "test-runner --suite=global-value --assert-on-failure" \
    --no-build \
    --gdb
```

At the GDB prompt:

```gdb
run
```

`--assert-on-failure` deliberately stops execution at the failed test
assertion, allowing the stack and local values to be inspected.

### 16.11 Detect memory and undefined-behavior errors

When Valgrind is installed:

```bash
./ns3 run my-simulation --no-build --valgrind
```

Run a test suite through Valgrind:

```bash
./test.py -g -s core
```

Valgrind is useful for:

- Invalid reads and writes
- Use-after-free
- Access to uninitialized memory
- Some memory leaks

It is significantly slower than a normal run. Use a small reproduction.
Valgrind support on Apple Silicon hosts is limited, but it can run inside a
compatible Linux container architecture.

Alternatively, create a sanitizer build:

```bash
./ns3 configure \
    --build-profile=debug \
    --enable-sanitizers \
    --enable-examples \
    --enable-tests \
    --disable-werror \
    -G Ninja

./ns3 build my-simulation -j 2
./ns3 run my-simulation --no-build
```

The ns-3 sanitizer option enables supported address, leak, and undefined
behavior checks. Sanitizer output normally includes an error type, stack
trace, and source location.

Do not reuse the same build directory when repeatedly switching between
substantially different debug configurations without reconfiguring it.

### 16.12 Debug with VS Code

The repository contains upstream `.vscode/launch.json` and `tasks.json`
templates. They are useful references, but their executable paths use generic
`ns3-dev` names and may not exactly match the `ns3.47` names generated by this
checkout.

A practical container workflow is:

1. Start the persistent container.
2. Attach VS Code to the running container.
3. Open `/workspace/ns-3`.
4. Build the target in the integrated terminal.
5. Point `launch.json` at the actual executable.

Start the container:

```bash
docker compose up -d
```

In VS Code, run:

```text
Dev Containers: Attach to Running Container
```

Select the ns-3 container and open:

```text
/workspace/ns-3
```

Build the example:

```bash
./ns3 build first -j 2
```

Discover the exact executable command and path:

```bash
./ns3 run first --no-build --dry-run
```

An example debug configuration is:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Debug ns-3 program in container",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/build/${input:ns3Executable}",
      "args": [],
      "cwd": "${workspaceFolder}",
      "stopAtEntry": true,
      "MIMode": "gdb",
      "miDebuggerPath": "/usr/bin/gdb",
      "externalConsole": false,
      "setupCommands": [
        {
          "description": "Enable GDB pretty printing",
          "text": "-enable-pretty-printing",
          "ignoreFailures": true
        }
      ]
    }
  ]
}
```

Verify the executable path before using this configuration because it changes
with the ns-3 version, target directory, and build profile.

### 16.13 Remote UI debugging from macOS to an Ubuntu Server

Use this topology when Docker runs on an Ubuntu Server while macOS is the
editing and debugging workstation:

```text
macOS
  VS Code UI
      |
      | SSH
      v
Ubuntu Server
  VS Code remote extension host
      |
      | attach to running container
      v
ns3 container
  /workspace/ns-3
  Linux compiler + Linux executable + GDB
```

This is preferable to launching GDB or LLDB locally on macOS. The debugger
must load Linux binaries, Linux shared libraries, and the container's source
paths; keeping the debug adapter and GDB inside the container avoids
architecture, binary-format, and path-mapping problems.

Install these VS Code extensions on macOS:

- Remote - SSH
- Dev Containers
- C/C++

Configure the Ubuntu server in `~/.ssh/config`:

```sshconfig
Host ns3-server
    HostName 192.0.2.10
    User developer
    IdentityFile ~/.ssh/id_ed25519
```

Replace the example address and user with the actual server values. Clone and
start the project on Ubuntu:

```bash
git clone <repository-url>
cd ns-3.47-dev
NS3_UID="$(id -u)" NS3_GID="$(id -g)" docker compose build
docker compose up -d
docker compose exec ns3 ./ns3 build first -j 2
```

From VS Code on macOS:

1. Run `Remote-SSH: Connect to Host`.
2. Select `ns3-server`.
3. Open the repository directory on the Ubuntu Server.
4. Run `Dev Containers: Attach to Running Container`.
5. Select the `ns3` service container.
6. Open `/workspace/ns-3`.
7. Install the C/C++ extension in the container when VS Code prompts.
8. Open `examples/tutorial/first.cc` and place a breakpoint in `main()`.
9. Use the container launch configuration from section 16.12.
10. Press F5 and inspect Variables, Watch, Call Stack, and Breakpoints in the
    macOS UI.

Although the UI is on macOS, the extension host, GDB, executable, libraries,
and build directory all remain on Linux. Source changes are saved directly to
the Ubuntu checkout mounted into the container.

When VS Code is connected to the Ubuntu host through Remote SSH but is not
attached to the container, select the checked-in
`Remote SSH: debug through Docker exec` profile. Its `pipeTransport` starts
`/usr/bin/gdb` using `docker compose exec -T ns3`, maps
`/workspace/ns-3` to the SSH workspace, and does not require gdbserver or port
2345. It prompts for an executable path relative to the container's `build/`
directory, so the selected editor tab does not affect program selection. See
`DOCKER.md` for the exact workflow.

The command-line workflow remains available at the same time:

```bash
docker compose exec ns3 ./ns3 run first --no-build --gdb
```

#### Optional remote GDB protocol

Some IDE configurations require `gdbserver`. The main `compose.yaml` publishes
the container's port 2345 on every Ubuntu Server interface:

```bash
docker compose up -d --build
```

Build the target and start one debug session:

```bash
docker compose exec ns3 ./ns3 build first -j 2
docker compose exec ns3 \
  gdbserver 0.0.0.0:2345 \
  /workspace/ns-3/build/examples/tutorial/ns3.47-first-debug
```

Use the dry-run command if the versioned executable name is different:

```bash
docker compose exec ns3 ./ns3 run first --no-build --dry-run
```

Find the Ubuntu Server IP address:

```bash
hostname -I
```

The UI debugger on macOS connects directly to `UBUNTU_SERVER_IP:2345`; an SSH
tunnel is not required. The Compose mapping is:

```yaml
ports:
  - "0.0.0.0:${NS3_GDB_PORT:-2345}:2345"
```

The GDB remote protocol provides neither authentication nor encryption.
Restrict access to the Mac's IP with the Ubuntu or network firewall:

```bash
sudo ufw allow from MAC_IP to any port 2345 proto tcp
```

Do not expose this port to the public Internet. Use a trusted LAN or VPN for
direct access.

The checked-in `.vscode/launch.json` also contains a
`Remote gdbserver: ns-3 first` profile for a VS Code UI running directly on
macOS. It prompts for `UBUNTU_SERVER_IP:2345`, maps the container source prefix
`/workspace/ns-3` to the local workspace, and reads matching symbols from the
git-ignored `remote-debug/` directory. Follow the symbol export and connection
steps in `DOCKER.md`.

For VS Code, Remote SSH plus Dev Containers is still the recommended mode.
Use raw `gdbserver` only when the selected IDE cannot attach its debugger
directly to the container.

### 16.14 Recommended debugging checklist

Use this sequence for most problems:

```text
1. Reproduce the issue consistently.
2. Reduce the topology, flows, packets, and simulation duration.
3. Fix the seed and run number.
4. Confirm the debug build and rebuild the affected target.
5. Enable one or two relevant log components.
6. Add a trace callback for the incorrect state or measurement.
7. Locate the responsible model and its tests.
8. Add focused breakpoints around the unexpected transition.
9. Inspect the stack, arguments, object state, and simulated time.
10. Use GDB, sanitizers, or Valgrind if memory safety is suspected.
11. Convert the reproduction into an automated test.
12. Verify that the fix does not change unrelated seeded results.
```

For every bug, record:

- Exact build profile and configuration
- Target name and command line
- Seed and run number
- Minimal topology
- Expected result
- Actual result
- First incorrect event or state transition
- Relevant log or trace output

## 17. Recommended Source-Reading Roadmap

### Phase 1: Tutorial programs

Read and run:

```text
examples/tutorial/first.cc
examples/tutorial/second.cc
examples/tutorial/third.cc
```

Use the per-file English guides in
[`examples/tutorial/docs/`](examples/tutorial/docs/README.md) while reading
these programs. The guide set covers every C++ tutorial source from
`hello-simulator.cc` through `seventh.cc`, including the reusable
`tutorial-app.cc`.

Goals:

- Create nodes and links
- Install an Internet stack
- Install applications
- Configure mobility and Wi-Fi
- Enable logging and tracing

### Phase 2: Simulator kernel

Read:

```text
src/core/model/simulator.*
src/core/model/default-simulator-impl.*
src/core/model/event-impl.*
src/core/model/time.*
src/core/model/object.*
src/core/model/type-id.*
src/core/model/attribute.*
```

Goal: understand scheduling, object lifetime, type registration, and
configuration.

### Phase 3: Network abstractions

Read:

```text
src/network/model/node.*
src/network/model/packet.*
src/network/model/net-device.*
src/network/model/channel.*
src/network/model/socket.*
src/network/model/application.*
```

Goal: understand how technology-independent networking objects relate to each
other.

### Phase 4: Point-to-point end-to-end flow

Read:

```text
src/point-to-point/helper/point-to-point-helper.*
src/point-to-point/model/point-to-point-net-device.*
src/point-to-point/model/point-to-point-channel.*
```

The point-to-point module is the easiest complete link model to study.

### Phase 5: Internet stack

Study in this order:

```text
UDP
IPv4
Routing
TCP
```

UDP provides a shorter path through the stack. TCP introduces connection
state, retransmission, congestion control, receive and transmit buffers, and
many timers.

### Phase 6: Research-specific module

Only after the earlier phases should you deeply study one of:

```text
wifi/
lte/
lr-wpan/
zigbee/
spectrum/
```

Wi-Fi and LTE are large subsystems. Starting with them before understanding the
core, network, and Internet abstractions makes the call flow much harder to
follow.

## 18. Practical Study Method

For each concept or module:

1. Read its documentation under `doc/` or `src/<module>/doc/`.
2. Run the smallest relevant example.
3. Find the helper called by the example.
4. Follow the helper into the created model classes.
5. Identify the scheduled events and callback entry points.
6. Enable one or two relevant log components.
7. Attach a trace callback.
8. Read the associated tests to learn expected behavior.
9. Change one parameter and predict the result before running.
10. Confirm the prediction using trace output.

Tests are particularly valuable because they describe precise behavior and
edge cases more directly than large examples.

## 19. The Three Workflows to Master

Most ns-3 source behavior can be organized into three recurring workflows.

### 19.1 Configuration workflow

```text
Helper
    → ObjectFactory
    → Object construction
    → TypeId
    → Attribute assignment
```

### 19.2 Packet runtime workflow

```text
Application
    → Socket
    → Transport protocol
    → Network protocol
    → Traffic control
    → NetDevice
    → Channel
    → Destination stack
```

### 19.3 Simulation workflow

```text
Simulator::Schedule
    → Event queue
    → Simulated time advances
    → Callback executes
    → New events may be scheduled
```

Once these three workflows are understood, individual ns-3 modules become much
easier to navigate. Most modules provide specialized protocol or physical
behavior while using the same object, attribute, callback, tracing, packet, and
event infrastructure.

## 20. Suggested First Exercises

### Exercise 1: Change link properties

Modify the first tutorial to accept data rate and delay from the command line.
Predict how propagation delay changes the Echo response time.

### Exercise 2: Send multiple packets

Increase `MaxPackets` and change the interval. Trace transmit and receive times
to verify the event schedule.

### Exercise 3: Add packet loss

Attach an error model to a receiving device. Measure the difference between
application-level sent and received packets.

### Exercise 4: Add FlowMonitor

Install FlowMonitor and report:

- Transmitted packets
- Received packets
- Lost packets
- Mean delay
- Throughput

### Exercise 5: Replace the link model

Recreate the two-node scenario using CSMA, then Wi-Fi. Compare how the helper,
device, channel, and PHY configuration changes while the application and
Internet layers remain similar.

### Exercise 6: Write a test

Create a small test case that schedules several events and verifies their
execution order. This provides direct experience with the test framework and
event scheduler.

## Conclusion

Do not attempt to memorize the entire repository. Understand the shared
framework and learn to follow control flow:

```text
Scenario construction
    → Helpers
    → Models and attributes
    → Scheduled events
    → Packet-processing layers
    → Trace and test results
```

Begin with the first tutorial and point-to-point module, then study the
simulator kernel and generic network abstractions. After those foundations are
clear, move into the Internet stack and finally the wireless or cellular module
relevant to your work.
