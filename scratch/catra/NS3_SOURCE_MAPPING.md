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

## CATRA MAC mapping status

- Paper `CWmin=32` and `CWmax=1024` are window cardinalities. ns-3 stores the
  inclusive maximum sampled by `UniformRandomVariable::GetInteger(0, cw)`, so
  their exact ns-3 representations are `31` and `1023`. The configured
  802.11b defaults already resolve to those values in
  `WifiMac::ConfigurePhyDependentParameters`.
- `Txop::GenerateBackoff` samples the next backoff and calls
  `StartBackoffNow`; `NotifyChannelReleased` generates the next backoff after
  a completed channel access.
- `Txop::ResetCw` restores `cwMin`; `UpdateFailedCw` applies
  `min(cwMax, 2^retry * (cwMin + 1) - 1)` before a subsequent backoff.
- CATRA arithmetic must use window cardinality `W = cw + 1`, then convert the
  result back to an inclusive ns-3 value `cw = W - 1`.

Still required before the first CW write:

- Definition and lifetime of `CW_original` used by the CATRA formula.
- A safe runtime hook at the new-backoff boundary that preserves retry/BEB
  state instead of asynchronously replacing a live `Txop` CW.

## Required before CATRA TCP

The following mapping must be completed before the first cwnd/delay write:

- `TcpSocketState::m_cWnd` ownership and byte/segment conversion.
- `m_ssThresh`, bytes-in-flight, highest ACK and current sequence ownership.
- NewReno ACK, loss, Fast Recovery and RTO call order.
- Safe controller hook preserving TCP recovery state.
- Application scheduling hook used for `deltaF` without blocking simulation.

The pure Algorithm 2 equations and decisions are implemented in
`tcp_rate_adaptation/catra-tcp-controller.{h,cc}` and validated independently. The remaining items
above concern runtime ownership/integration and are intentionally not hidden
inside the pure controller.
