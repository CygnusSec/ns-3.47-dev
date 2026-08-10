# ns-3.47-dev Architecture and the `fifth` Example

This document explains the complete ns-3 module structure, the principal runtime classes, the
project lifecycle, and the end-to-end TCP packet flow in `examples/tutorial/fifth.cc`.

All diagrams use plain ASCII text so they work in every Markdown viewer.

> ns-3 contains more than 3,000 files and 44 modules under `src/`. A diagram containing every
> implementation class would be unreadable. This document includes every module and the central
> abstractions through which implementation classes communicate.

## 1. What ns-3 is

ns-3 is a collection of C++ libraries for **discrete-event network simulation**.

```text
+------------------+     +------------------+     +------------------+     +------------------+
| 1. Configuration | --> | 2. Execution     | --> | 3. Observation   | --> | 4. Cleanup       |
| Create topology  |     | Simulator::Run() |     | Trace/statistics |     | Destroy()        |
+------------------+     +------------------+     +------------------+     +------------------+
```

Configuration creates objects and schedules events. During `Simulator::Run()`, simulated time jumps
from one event timestamp to the next. Models normally run sequentially, not in separate threads.

## 2. Repository map

| Path | Responsibility |
|---|---|
| `src/` | The 44 official ns-3 libraries/modules |
| `contrib/` | Extension modules outside the standard set |
| `examples/` | Complete examples, including `tutorial/fifth.cc` |
| `scratch/` | User simulation workspace |
| `bindings/` | Runtime C++/Python integration through cppyy |
| `build-support/`, `CMakeLists.txt` | Build logic, dependencies, executables, tests |
| `build/` | Generated artifacts |
| `doc/` | Manuals, tutorials, and architecture documentation |
| `utils/`, `test.py` | Development utilities and regression tests |
| `third-party/` | Bundled third-party source |
| `ns3` | CLI wrapper for configuring, building, and running |

## 3. Complete module architecture

An arrow means “uses services or abstractions from the layer below.” Exact link dependencies are in
`src/<module>/CMakeLists.txt`.

```text
USER PROGRAMS
+------------------------------------------------------------------------------------------+
| examples / scratch / external programs | Python bindings | Helper and Container APIs     |
+-----------------------------------------+------------------------------------------------+
                                          |
                                          v
APPLICATIONS, OBSERVATION, CONFIGURATION
+------------------------------------------------------------------------------------------+
| applications | internet-apps | flow-monitor | stats | netanim | visualizer | config-store|
| debug-tools | test                                                                       |
+-----------------------------------------+------------------------------------------------+
                                          |
                                          v
INTERNET AND ROUTING
+------------------------------------------------------------------------------------------+
| internet: IPv4, IPv6, ARP, ICMP, UDP, TCP, sockets, routing interfaces                    |
| aodv | dsdv | dsr | olsr | nix-vector-routing | traffic-control | sixlowpan              |
+-----------------------------------------+------------------------------------------------+
                                          |
                                          v
LINK TECHNOLOGIES, EMULATION, TOPOLOGY
+------------------------------------------------------------------------------------------+
| point-to-point | csma | bridge | virtual-net-device                                       |
| wifi | mesh | lr-wpan | zigbee | uan | lte                                                |
| fd-net-device | tap-bridge | click | openflow                                             |
| point-to-point-layout | csma-layout | topology-read | brite                               |
+-----------------------------------------+------------------------------------------------+
                                          |
                                          v
PHYSICAL ENVIRONMENT AND RESOURCES
+------------------------------------------------------------------------------------------+
| mobility | propagation | buildings | spectrum | antenna | energy                          |
+-----------------------------------------+------------------------------------------------+
                                          |
                                          v
SIMULATION FOUNDATION
+------------------------------------------------------------------------------------------+
| network: Node, Application, Packet, Socket, NetDevice, Channel, Queue                      |
| core: Simulator, Scheduler, Event, Time, Object, TypeId, Attribute, Callback, RNG, Logging|
+------------------------------------------------------------------------------------------+
                                          ^
                                          |
                                 mpi integrates here
```

### 3.1 Major dependency paths

```text
User program
  +--> Helpers -------------> create and configure model objects
  +--> Applications --------> internet ------> network ------> core
  |                              +-----------> traffic-control
  |                              +-----------> routing modules
  +--> Wired devices --------> network ------> core
  +--> Wireless devices -----> network ------> core
                +-----------> mobility
                +-----------> propagation
                +-----------> spectrum ------> antenna
                +-----------> energy
```

### 3.2 Responsibilities of all 44 modules

| Group | Module | Main responsibility |
|---|---|---|
| Foundation | `core` | Events, time, objects, types, attributes, callbacks, tracing, RNG, logging, tests |
| Foundation | `network` | Node, Application, Packet, Header, Tag, Socket, NetDevice, Channel, queue |
| Internet | `internet` | IPv4/IPv6, ARP, ICMP, UDP, TCP, sockets, interfaces, routing |
| Applications | `applications` | PacketSink, BulkSend, OnOff, UDP applications, traffic models |
| Applications | `internet-apps` | Ping, DHCP, supporting Internet applications |
| Wired | `point-to-point` | Two-ended device/channel, queue, PPP framing |
| Wired | `csma` | Ethernet-like shared CSMA medium |
| Wired | `bridge` | Layer-2 bridge between NetDevices |
| Wired | `virtual-net-device` | Virtual device delegating I/O to callbacks |
| Wireless | `wifi` | IEEE 802.11 PHY, MAC, rate control, access, management |
| Wireless | `mesh` | Wi-Fi mesh and HWMP |
| Wireless | `lr-wpan` | IEEE 802.15.4 low-rate WPAN |
| Wireless | `zigbee` | Zigbee over LR-WPAN |
| Wireless | `uan` | Underwater acoustic networking |
| Cellular | `lte` | LTE/EPC, RLC, PDCP, RRC, schedulers, core-network models |
| Routing | `aodv` | Ad hoc On-Demand Distance Vector |
| Routing | `dsdv` | Destination-Sequenced Distance Vector |
| Routing | `dsr` | Dynamic Source Routing |
| Routing | `olsr` | Optimized Link State Routing |
| Routing | `nix-vector-routing` | Compact source routing with Nix vectors |
| Queueing | `traffic-control` | Queue disciplines, filters, flow control, queue management |
| Low-power IP | `sixlowpan` | IPv6 adaptation over low-power links |
| Radio | `mobility` | Position, velocity, mobility models |
| Radio | `propagation` | Path loss, delay, fading models |
| Radio | `spectrum` | Signal spectra, interference, spectrum channels |
| Radio | `antenna` | Antenna gain, pattern, orientation |
| Radio | `buildings` | Indoor/outdoor and building propagation |
| Resources | `energy` | Energy sources, device models, harvesters |
| Measurement | `flow-monitor` | Per-flow delay, loss, jitter, throughput |
| Measurement | `stats` | Probes, calculators, data collection, outputs |
| Visualization | `netanim` | XML output for NetAnim |
| Visualization | `visualizer` | Interactive visualization |
| Configuration | `config-store` | Attribute configuration persistence |
| Debugging | `debug-tools` | Topology and packet-flow diagnostics in this repository |
| Parallel | `mpi` | Supported distributed simulations through MPI |
| Emulation | `fd-net-device` | File descriptor, TAP, and raw-socket integration |
| Emulation | `tap-bridge` | Connection to a host TAP device |
| Integration | `click` | Click Modular Router integration |
| Integration | `openflow` | OpenFlow switch and controller models |
| Topology | `point-to-point-layout` | Common point-to-point topologies |
| Topology | `csma-layout` | Common CSMA topologies |
| Topology | `topology-read` | Topology import from files/datasets |
| Topology | `brite` | BRITE topology generation/import |
| Testing | `test` | Shared test models and test module |

## 4. Core class relationships

```text
ObjectBase
  +-- Object
       +-- Node
       |    +-- owns Applications and NetDevices
       |    +-- aggregates IPv4, IPv6, TCP, UDP, ARP, routing, etc.
       +-- Application
       |    +-- TutorialApp, PacketSink, OnOffApplication, BulkSendApplication, ...
       +-- Socket
       |    +-- TcpSocketBase, UDP socket implementation
       +-- NetDevice
       |    +-- PointToPointNetDevice, CsmaNetDevice, WifiNetDevice, ...
       +-- Channel
       |    +-- PointToPointChannel, CsmaChannel, WifiChannel, ...
       +-- Ipv4L3Protocol / Ipv6L3Protocol / TcpL4Protocol / ...

TypeId describes inheritance, constructors, attributes, and trace sources.
Ptr<T> provides intrusive reference counting. CreateObject<T>() normally creates Object instances.
```

### Runtime call chain

```text
Application
  -> Socket::Send(Packet)
  -> Socket implementation (TcpSocketBase or UDP socket)
  -> L4 protocol (TCP or UDP)
  -> L3 protocol (IPv4 or IPv6)
  -> routing and interface selection
  -> NetDevice::Send()
  -> queue, MAC, PHY
  -> Channel schedules arrival
  -> receiving NetDevice -> L3 -> L4 -> Socket -> Application
```

### Helper API versus model API

```text
CONFIGURATION TIME
main() --> NodeContainer ---------> creates Nodes
       --> PointToPointHelper ----> creates devices and channel
       --> InternetStackHelper ---> aggregates ARP/IP/ICMP/UDP/TCP/routing
       --> Ipv4AddressHelper -----> assigns addresses
       --> PacketSinkHelper ------> creates sink and listening socket

SIMULATION TIME
Application --> Socket --> Protocol models --> NetDevice --> Channel
```

Helpers create and configure objects; they do not process packets at runtime.

## 5. Event scheduler

```text
main()          Simulator       DefaultSimulatorImpl      Scheduler      Model
  |                 |                    |                    |             |
  |-- Schedule() -->|-- Schedule() ---->|-- Insert(event) -->|             |
  |-- Run() ------->|-- Run() --------->|                    |             |
  |                 |                    |-- RemoveNext() --->|             |
  |                 |                    |<-- event ----------|             |
  |                 |                    |-- set Now ---------------------->|
  |                 |                    |<----------------------- schedule|
  |                 |                    |-- Insert(new event) -->|         |
  |                 |                    | repeat until Stop/no events      |
  |<-- return ------|<-------------------|                    |             |
```

Events represent application start/stop, frame serialization, propagation arrival, TCP timeout,
mobility updates, and PHY/MAC state changes. The smallest timestamp runs first; event IDs preserve
ordering when timestamps are equal.

## 6. General project flow

```text
Parse options -> Create Nodes -> Create Devices/Channels -> Install protocols and routing
      -> Install Applications/Sockets -> Connect traces -> Schedule events -> Run
      -> Process next event -> Schedule future events -> Stop -> Read results -> Destroy
```

## 7. The `fifth` topology

```text
Node 0: sender                                          Node 1: receiver
10.1.1.1                                                10.1.1.2:8080

+-------------------------+                             +-------------------------+
| TutorialApp             |                             | PacketSink              |
| 1000 x 1040 B @ 1 Mbps  |                             | counts received bytes   |
+------------+------------+                             +------------^------------+
             | Socket::Send                                          |
             v                                                       |
+-------------------------+                             +-------------+-----------+
| TcpSocketBase/NewReno   |<---------- TCP ACKs -------| TcpSocketBase           |
+------------+------------+                             +-------------^-----------+
             v                                                       |
+-------------------------+                             +-------------+-----------+
| Ipv4L3Protocol          |                             | Ipv4L3Protocol          |
+------------+------------+                             +-------------^-----------+
             v                                                       |
+-------------------------+      PointToPointChannel    +-------------+-----------+
| PointToPointNetDevice   |===== 5 Mbps, 2 ms delay ===>| PointToPointNetDevice   |
+-------------------------+                             | Error rate: 0.00001     |
                                                        +-------------------------+
```

| Component | Value |
|---|---|
| Link | Point-to-point, `5 Mbps`, one-way delay `2 ms` |
| Flow | `10.1.1.1` to `10.1.1.2:8080`, `/30` subnet |
| Sender | 1,000 calls creating 1,040-byte packets at `1 Mbps` offered rate |
| Receiver | TCP `PacketSink` |
| TCP | `TcpNewReno`, initial cwnd one segment, `TcpClassicRecovery` |
| Loss | `RateErrorModel(0.00001)` on node 1's receive device |
| Timing | Sink at `t=0`; sender at `t=1`; stop at `t=20` |

## 8. Object graph created by `fifth`

```text
fifth::main()
  +-- NodeContainer::Create(2) --> Node 0, Node 1
  +-- PointToPointHelper::Install()
  |    +-- two PointToPointNetDevices --> one PointToPointChannel
  +-- RateErrorModel --> Node 1 device receive path
  +-- InternetStackHelper::Install()
  |    +-- ARP, IP, ICMP, UDP, TCP, routing on each Node
  +-- PacketSinkHelper::Install(Node 1)
  |    +-- PacketSink + listening/accepted TCP sockets
  +-- Socket::CreateSocket(Node 0, TcpSocketFactory)
  |    +-- TcpSocketBase using TcpNewReno
  +-- CreateObject<TutorialApp>() --> Node 0, using sender socket
```

## 9. `fifth` timeline

```text
t = 0 s       PacketSink binds, listens, and installs receive callback
t = 1 s       TutorialApp starts, binds/connects socket, calls SendPacket()
every 8.32 ms TutorialApp creates 1,040 bytes and calls Socket::Send()
on loss       PhyRxDrop fires; TCP later detects loss and retransmits
t = 20 s      Run stops; main reads GetTotalRx(); Destroy releases state
```

```text
tNext = 1040 bytes * 8 / 1,000,000 bits/s = 0.00832 s = 8.32 ms
```

This is the offered application rate. TCP may transmit later because of connection state, congestion
window, receive window, buffering, ACKs, or loss recovery.

## 10. Complete TCP packet call path

```text
TutorialApp::SendPacket()
  -> Create<Packet>(1040)
  -> Socket::Send(packet)
  -> TcpSocketBase stores bytes in TcpTxBuffer
  -> check TCP state, cwnd, and bytes in flight
  -> create segment according to MSS
  -> TcpL4Protocol::SendPacket() adds TCP header
  -> Ipv4L3Protocol::Send() selects route and adds IPv4 header
  -> PointToPointNetDevice::Send() queues frame
  -> device dequeues and starts transmission
  -> PointToPointChannel::TransmitStart()
  -> schedule arrival after serialization time + 2 ms
  -> receiving PointToPointNetDevice
  -> RateErrorModel::IsCorrupt()
       +-- corrupt: PhyRxDrop; no delivery; TCP later detects missing ACK/data
       +-- valid: Ipv4L3Protocol local delivery
            -> receiving TCP and TcpSocketBase
            -> sequence processing and byte-stream reassembly
            -> PacketSink::HandleRead() -> Socket::RecvFrom()
            -> update PacketSink total bytes
            -> ACK returns through the reverse stack
            -> sender updates RTT, bytes in flight, cwnd, and timers
```

### Where packet sending begins

This only schedules the application:

```cpp
app->SetStartTime(Seconds(1.));
```

At `t=1 s`, the scheduler calls:

```cpp
void TutorialApp::StartApplication()
{
    m_socket->Bind();
    m_socket->Connect(m_peer);
    SendPacket();
}
```

The application creates data and passes it to TCP here:

```cpp
void TutorialApp::SendPacket()
{
    Ptr<Packet> packet = Create<Packet>(m_packetSize);
    m_socket->Send(packet);
}
```

Do not confuse three moments:

1. `SetStartTime(1 s)` schedules the lifecycle event.
2. `Socket::Send()` offers bytes to TCP.
3. `PointToPointChannel::TransmitStart()` starts frame transmission on the simulated link.

## 11. Encapsulation

```text
Application payload --> add TCP header --> TCP segment
  --> add IPv4 header --> IPv4 packet
  --> add link framing --> Link frame
  == point-to-point channel ==>
  remove framing --> remove IPv4 header --> TCP reassembly --> PacketSink byte stream
```

TCP is a byte-stream protocol. One 1,040-byte application `Send()` does not necessarily correspond
to one TCP segment or one link frame.

## 12. Congestion control, loss, and tracing

```text
Handshake -> Slow Start --cwnd >= ssthresh--> Congestion Avoidance
                  |                                  |
                  +------ loss/duplicate ACK --------+
                                     |
                               Fast Recovery
                                     |
                         recovery complete or RTO
```

- `CongestionWindow` invokes `CwndChange` when cwnd changes.
- `RateErrorModel` drops frames below IP/TCP.
- TCP infers loss through duplicate ACKs or retransmission timeout.
- `PhyRxDrop` invokes `RxDrop` at the receiving device.
- Optional traces cover `SlowStartThreshold`, `CongState`, `State`, `RTT`, `RTO`,
  `BytesInFlight`, and `Retransmission`.
- `PacketSink::GetTotalRx()` counts application bytes, excluding protocol headers.

Trace callbacks run synchronously where a model emits them; they do not create another thread.

## 13. Build-time versus runtime

```text
BUILD TIME
src/*/CMakeLists.txt --> module libraries --> fifth executable
fifth.cc + tutorial-app.cc -----------------> fifth executable

RUNTIME
./ns3 run fifth --> object graph --> event queue --> logs, traces, summary
```

CMake determines linked libraries. The object graph determines topology. Scheduled events determine
runtime behavior.

## 14. How to read and extend ns-3

Follow an example in this order:

1. `main()` for topology, helpers, attributes, applications, and timing.
2. Application lifecycle, send methods, and receive callbacks.
3. The corresponding socket implementation.
4. IPv4/IPv6 and routing.
5. The concrete device, queue, and channel.
6. The receiver in reverse order.
7. `Simulator::Schedule*()` calls that defer work.
8. `TypeId`, attributes, and trace sources.

```text
model/<class>.h/.cc
  +--> TypeId, attributes, trace sources
  +--> helper/<helper>.h/.cc --> example/user API
  +--> test/*-test-suite.cc
  +--> documentation and examples
```

## 15. Source-code entry points

| Topic | Start reading here |
|---|---|
| Simulator/event loop | `src/core/model/simulator.{h,cc}`, `default-simulator-impl.{h,cc}` |
| Object/TypeId/attributes | `src/core/model/object.*`, `type-id.*`, `attribute.*`, `config.*` |
| Node/Application lifecycle | `src/network/model/node.*`, `application.*` |
| Packet representation | `src/network/model/packet.*`, `buffer.*`, `header.*`, `tag.*` |
| Socket abstraction | `src/network/model/socket.*` |
| TCP | `src/internet/model/tcp-socket-base.*`, `tcp-l4-protocol.*`, `tcp-tx-buffer.*` |
| IPv4 | `src/internet/model/ipv4-l3-protocol.*`, `ipv4-interface.*` |
| Point-to-point | `src/point-to-point/model/point-to-point-net-device.*`, `point-to-point-channel.*` |
| Receiver | `src/applications/model/packet-sink.*` |
| `fifth` sender | `examples/tutorial/tutorial-app.*` |
| `fifth` configuration | `examples/tutorial/fifth.cc` |
| Module dependencies | `src/*/CMakeLists.txt` |

## 16. Project flow in one line

```text
Example configures objects -> helpers create models -> applications schedule events -> Simulator
executes events by timestamp -> packets cross Application/Socket/Transport/IP/NetDevice/Channel ->
receiver processes the reverse stack -> traces record results -> Destroy releases simulation state.
```
