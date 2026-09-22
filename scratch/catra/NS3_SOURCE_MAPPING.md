# CATRA Scenario 1 — ns-3.47 Source Mapping

This file records source-level ownership used by the implementation. Entries
are completed phase by phase; a later controller must not be implemented while
its required ownership or unit remains unresolved.

## n=3 routing phase

| Scenario concept | ns-3 owner/API | Use | Evidence |
|---|---|---|---|
| Static routing implementation | `Ipv4StaticRoutingHelper` | Install only static routing in every station | `src/internet/helper/ipv4-static-routing-helper.*` |
| Obtain node routing table | `Ipv4StaticRoutingHelper::GetStaticRouting` | Retrieve the node-owned `Ipv4StaticRouting` | Helper API is `const` and searches direct/list routing |
| Force next hop | `Ipv4StaticRouting::AddHostRouteTo(destination, nextHop, interface)` | Add `/32` forward/reverse route | `src/internet/model/ipv4-static-routing.cc` |
| Wi-Fi IPv4 interface | `Ipv4::GetInterfaceForDevice` | Resolve output interface without assuming index 1 | Runtime lookup from installed device |
| Probe generator | `UdpClientHelper` | Send five sequence-bearing UDP packets | `src/applications/helper/udp-client-server-helper.*` |
| Probe receiver | `UdpServerHelper` | Count UDP delivery without reply traffic | Same helper/module |
| IP forwarding count | `FlowMonitor::FlowStats::timesForwarded` | Count relay forwarding operations | `src/flow-monitor/model/flow-monitor.*` |
| Flow identity | `Ipv4FlowClassifier::FiveTuple` | Select probes by UDP destination port | `src/flow-monitor/model/ipv4-flow-classifier.*` |

For a delivered unicast flow:

```text
observed_hops = 1 + timesForwarded / rxPackets
```

Expected values for `n=3`:

```text
S2(10.1.1.1) -> S1(10.1.1.2) -> R(10.1.1.3): 2 hops
R(10.1.1.3)  -> S1(10.1.1.2) -> S2(10.1.1.1): 2 hops
S1(10.1.1.2) -> R(10.1.1.3):                    1 hop
```

Host routes are required even though all Wi-Fi addresses use `10.1.1.0/24`.
The `/32` prefix is more specific than the connected `/24` route and prevents a
distant destination from being treated as a directly reachable Wi-Fi neighbor.

## Existing Algorithm 1 measurement mapping

| Paper concept | ns-3 owner/API | Access | Unit |
|---|---|---|---|
| Current CW | station `Txop::GetCw(0)` | read-only | backoff slots |
| MAC DATA RX | `MonitorSnifferRx` through `ObserveMacFrameRx` | observe | frame event |
| Normal MAC ACK TX | `MonitorSnifferTx` through `ObserveMacFrameTx` | observe/correlate | frame event |
| TCP DATA/TCP ACK classification | LLC SNAP -> IPv4 -> TCP parser | observe | packet type |
| Transaction timing | `WifiPhy::CalculateTxDuration` + PHY slot/SIFS | calculate | ns-3 `Time` |
| Period accumulator | `CatraActiveTimeEstimator` | read-only measurement | ns-3 `Time` |
| `RBRs` | smoothed active time / EP | calculate | dimensionless |

The existing probe does not write CW and is not a CATRA MAC controller.

## Required before CATRA MAC

The following mapping must be completed before the first CW write:

- Exact owner and call order of backoff generation.
- Exact CW update/reset sequence after success and retry failure.
- Paper `CWmin=32`, `CWmax=1024` mapping to ns-3 inclusive slot values.
- Definition and lifetime of `CW_original` used by the CATRA formula.
- Safe runtime hook that does not bypass DCF retry/BEB state.

## Required before CATRA TCP

The following mapping must be completed before the first cwnd/delay write:

- `TcpSocketState::m_cWnd` ownership and byte/segment conversion.
- `m_ssThresh`, bytes-in-flight, highest ACK and current sequence ownership.
- NewReno ACK, loss, Fast Recovery and RTO call order.
- Safe controller hook preserving TCP recovery state.
- Application scheduling hook used for `deltaF` without blocking simulation.
