# Scenario 1 and CATRA: ns-3.47 Assumptions and Configuration Contract

## Purpose

This document is the source of truth for porting Scenario 1, "Long vs. Short
Hop Flows," from the CATRA paper and its NS-2 terminology to this ns-3.47
checkout. It separates values stated by the paper from porting decisions and
from parameters that still require measurement.

No CATRA control behavior may be implemented until the baseline acceptance
gates in this document pass. A parameter marked `CALIBRATE` or `UNRESOLVED`
must not be presented as a value taken from the paper.

## Directory layout

Scenario 1 is a common simulation, independent of whether the optional CATRA
module is enabled. CATRA probes remain grouped separately:

```text
scratch/scenario1/
├── scenario1-main.cc                 # common CLI and orchestration
├── scenario1-traffic.cc/.h           # Flow1, Flow2, optional TCP stress
├── scenario1-radio.cc/.h             # radio relationships and flow counts
├── scenario1-cw-trace.cc/.h          # common DCF/BEB tracing
├── scenario1-mac-hop-measurement.cc/.h
├── tcp-tahoe.cc/.h                   # common Tahoe-compatible TCP
└── CMakeLists.txt                     # libactive-time + optional libcatra

scratch/catra/
├── active-time-estimation/
│   └── catra-active-time-probe.cc
├── tcp_rate_adaptation/
│   └── catra-tcp-controller-probe.cc
└── catra-phy-range-probe.cc

contrib/active-time/                   # common passive Tactive/RBR measurement
└── model/measurement/                # packet parsing and MAC ACK correlation

contrib/catra/                         # optional CATRA controllers
├── model/mac/catra-mac-controller.*  # CW' decision, Equation (5)
└── model/tcp/catra-tcp-controller.*  # Algorithm 2 decision core
```

`CMakeLists.txt` declares each executable separately because both C++ files
contain `main()`. Executable names remain independent of their physical source
directories, including `catra-tcp-controller-probe` for Algorithm 2.

## Source classification

| Status | Meaning |
|---|---|
| `PAPER` | Explicitly stated by the CATRA paper |
| `PORT` | A documented ns-3.47 implementation decision |
| `CALIBRATE` | Must be selected from measured PHY behavior |
| `UNRESOLVED` | Requires the authors' NS-2 source or an explicit later decision |

## Scope and phase gates

The work is divided into independently testable phases:

1. Phase 0: lock assumptions and naming (this document).
2. Phase 1: calibrate PHY behavior with a standalone range probe.
3. Phase 2: build the calibrated chain topology and assign IPv4 addresses.
4. Phase 3: install deterministic static routes and validate both directions with UDP.
5. Phase 4: add the Original TCP baseline and measurements.
6. Phase 5: run and validate the `n=3..6` baseline matrix.
7. Phase 6: add read-only CATRA measurements, then CATRA MAC control.
8. Phase 7: add CATRA TCP control and perform the final comparison.

Passing a later phase does not waive an earlier acceptance gate.

## Topology contract

| Item | Value | Status | Notes |
|---|---:|---|---|
| Station count | `n = 3, 4, 5, 6` | `PAPER` | One run uses one value of `n` |
| Node spacing | 200 m | `PAPER` | Nodes form a straight, stationary chain |
| `S2` index | `0` | `PORT` | Source of the long flow |
| `S1` index | `n - 2` | `PORT` | Source of the one-hop flow |
| `R` index | `n - 1` | `PORT` | Common receiver |
| Node position | `(i * 200, 0, 0)` m | `PORT` | Antenna height is configured in the propagation model |
| Mobility model | `ConstantPositionMobilityModel` | `PORT` | No movement during a run |
| Flow 1 | `S1 -> R`, port 5001 | `PORT` | Exactly one hop |
| Flow 2 | `S2 -> R`, port 5002 | `PORT` | Exactly `n - 1` hops |

### Phase 2 topology record

`scratch/scenario1/scenario1-main.cc` also provides a topology-only probe. It installs
the calibrated shared 802.11b ad-hoc channel, constant positions, one Wi-Fi
device and IPv4 address per station, and the static-routing implementation
without populating destination-specific routes.

Strict runs for `n=3,4,5,6` passed node count, role mapping, linear position,
200 m adjacent spacing, Wi-Fi device count, IPv4 interface count, and
structural hop-count checks. Phase 2 intentionally reports:

```text
catra=disabled
routing=static-unpopulated
applications=0
traffic=none
```

Phase 3 owns static host-route population and bidirectional UDP validation.

### Phase 3 n=3 implementation record (runtime validated)

`scenario1` accepts `--mode=route-probe`. The mode installs explicit
`/32` routes toward `R` through the next station on the right and explicit
reverse routes toward `S2` through the next station on the left. Three small
UDP probes exercise:

```text
long_forward:  S2 -> R, expected 2 hops when n=3
long_reverse:  R -> S2, expected 2 hops when n=3
short_forward: S1 -> R, expected 1 hop
```

FlowMonitor validates delivery and derives the observed IP hop count as:

```text
observed_hops = 1 + timesForwarded / rxPackets
```

The topology-only behavior remains available through `--mode=topology` and
does not install applications or populated host routes. The `n=3` route probe
passed all delivery and hop-count checks on 2026-09-22. Re-run it with:

```bash
docker compose exec -T ns3 ./ns3 build scenario1 -j 2
docker compose exec -T ns3 ./ns3 run \
  "scenario1 --mode=route-probe --n=3 --strict=true --printTopology=false" \
  --no-build
```

The validated run reported both `route_probe_overall=PASS` and
`scenario_overall=PASS`.

### Phase 4 n=3 TCP implementation record (runtime validated)

`--mode=scenario1` installs both saturated flows, two receiver sinks, the same
deterministic routes used by the probe, and the local `Scenario1TcpTahoe` port. It
uses Reno slow start/congestion avoidance, disables SACK, and replaces fast
recovery with Tahoe's loss response (`cwnd = 1 MSS`, then slow start). The
default run lasts 300 s; both sources start at 1 s.

The executable prints per-flow goodput and appends one row to
`results/scenario1/scenario1-throughput.csv`. `paper_total_approx_mbps` is Equation
(17)'s approximation, not a direct measurement of channel payload throughput.

Run the complete current `n=3` gate and terminal chart with:

```bash
scripts/catra/run-scenario1-n3.sh
```

For a quick functional smoke run, shorten only the simulation duration:

```bash
SIM_TIME=10 scripts/catra/run-scenario1-n3.sh /tmp/catra-scenario1-smoke.csv
```

The paper-aligned result still requires the default 300 s run. In the current
debug build, 10 simulated seconds take about 11 wall-clock seconds, so a full
run takes several minutes and writes its CSV row only after simulation ends.

The runner always executes the bidirectional UDP route probe before the TCP
baseline. A full 300 s Tahoe run on 2026-09-22 reported
`scenario_overall=PASS` with Flow 1 = 1.554453 Mbps, Flow 2 = 0.881722 Mbps,
and the paper Equation (17) approximation = 3.317897 Mbps for seed/run 1/1.
FlowMonitor independently confirmed the one-hop and two-hop paths. A smoke run
with `NS_LOG=Scenario1TcpTahoe=level_info` also observed loss responses resetting
the congestion window to one 1024-byte MSS.

### Algorithm 1 active-time measurement record

The read-only `CatraActiveTimeEstimator` implements Algorithm 1's two packet
conditions, complete modeled transaction-time sum, two-second period, and
exponential smoothing rule. `MonitorSnifferRx` preserves the `WifiMacHeader`, so
`Addr1 == local MAC` implements `p.destID == localID`. A received MAC DATA frame
carrying TCP-DATA is stored as pending and counted only when `MonitorSnifferTx`
observes the local station transmit its paired normal MAC ACK. A received MAC
DATA frame carrying a pure TCP ACK represents the second branch. No additional
flow-direction filter is applied because Algorithm 1 does not specify one.

For every accepted TCP DATA or TCP ACK, it reads the sender's current CW and
computes:

```text
t += 0.5 * CW * ST + T_RTS + SIFS + T_CTS + SIFS
     + T_TCP_DATA_OR_ACK + SIFS + T_MAC_ACK + DIFS
```

The individual frame durations come from `WifiPhy::CalculateTxDuration` using
the sender/receiver station managers' actual TX vectors. `ST` and `SIFS` come
from the sender PHY, while `DIFS = SIFS + 2 * ST` for this non-QoS 802.11b DCF
probe. RTS/CTS is forced with `RtsCtsThreshold=0` so the simulated exchange and
the paper formula have the same protection sequence.

At every estimation period boundary:

```text
TActive = 0.8 * previous_TActive + 0.2 * current_active_time
RBRs = TActive / EP
```

The accumulator `t` and per-period packet/component counters are reset after
each report, while smoothed `TActive` is retained as EWMA history. The estimator
only reads CW; it never changes it. This is an ns-3.47 `PORT` of Algorithm 1
rather than a claim that the paper's NS-2 header representation exists unchanged.
The first branch is reconstructed as one logical transaction from two physical
frames: the received TCP-DATA MAC DATA frame and the transmitted MAC ACK. This
preserves both header tests without claiming that a physical ACK contains TCP.
Both observations are assembled into `CatraAlgorithmPacket p`, and
`ProcessAlgorithmPacket` follows the paper verbatim: test `destID == localID`,
then `ACK && TCPDATA`, otherwise `DATA && TCPACK`. No extra classifier condition
is inserted between those tests and the active-time accumulator.

`RBRs = TActive / EP` is reported by the standalone probe. In the common
Scenario 1 executable, enabling the CATRA module creates one estimator and MAC
transaction tracker per station, derives `nSEND`, `nTX`, `nCS`, `ntotal` and
`FBRs`, writes `station-state.csv`, and applies the resulting CW'. Disabling
the module leaves the same topology, routes, TCP Tahoe flows and common output
pipeline intact without constructing those CATRA objects.

Run the Scenario 1 Algorithm 1 time series with:

```bash
scripts/catra/run-algorithm1-scenario1.sh
```

`STATIONS`, `SIM_TIME`, `EP`, `RUN`, and `OUTPUT_DIR` may be overridden;
`STATIONS="3 4 5 6"` runs the complete Scenario 1 station-count matrix. The
station CSV includes `RBRs`, raw/smoothed active time, packet composition,
average observed CW, and every transaction-time component for each EP. It also
prints the CATRA CW decision:

```text
ratio = RBRs / FBRs
CW'_raw = min(ratio * current_CW, CWmax)
CW'_slots = round(CW'_raw)
CW'_ns3 = CW'_slots - 1
```

`current_CW` and `CW'` above are window cardinalities in slots; the `_ns3`
columns are inclusive upper bounds used by ns-3. `decision` is
`DECREASE_CW`, `INCREASE_CW`, `KEEP_CW`, or
`NO_DATA_SEND_FLOW_FBR_ZERO`. The final value is expected for receiver `R` in
paper Table 1: `nSEND=0`, `nTX=2`, `nCS=1`, `ntotal=3`, and `FBR=0`. TCP ACK
airtime is measured for RBR, but TCP ACK is explicitly excluded from the
paper's competing SEND-flow count. The paper does not specify integer rounding,
so the CSV retains both the raw value and the explicit nearest-slot porting
choice.

### Algorithm 2 decision implementation

`contrib/catra/model/tcp/catra-tcp-controller.{h,cc}` contains the side-effect-free
Algorithm 2 decision core. All ns-3 TCP window inputs use byte units and
`bytesInFlight` replaces the algebraically equivalent
`currentSequence-highestAck` term, avoiding TCP sequence wrap-around errors:
then to MSS units before evaluating the paper equations:

```text
FBRf = 1 / ntotal                         # calculated internally
RBRf = TActiveFlow / EP                  # calculated internally
Ttr_f = TActiveFlow / Nf
win = (cwndBytes - bytesInFlight) / MSS
Tf = ntotal * win * Ttr_f
ratio = RBRf / FBRf
```

For `ratio > 1.05`, the decision reduces `cwnd` by one MSS (with a one-MSS
floor) and returns `deltaF = ratio * Tf`. For `ratio < 0.7`, it increases
`cwnd` by one MSS and returns zero delay. Equality at either threshold selects
the original TCP path, matching the strict inequalities in Algorithm 2.

The `catra-tcp-controller-probe` target validates the equations, threshold
boundaries, one-MSS floor, and inactive-flow handling. This is the verified
Algorithm 2 core, not yet a claim that Scenario 1 applies its decisions to a
live socket: per-flow MAC accounting and the socket/application delay hook
remain integration work.

The probe enables detailed logging by default. Each case prints the inputs,
TCP state, thresholds, ratio, `Ttr_f`, `win`, `Tf`, selected branch, cwnd
change, and `deltaF`. Use `--verbose=false` when only PASS/FAIL output is
needed.

Run the strict probe with:

```bash
docker compose exec -T ns3 ./ns3 build catra-active-time-probe -j 2
docker compose exec -T ns3 ./ns3 run catra-active-time-probe --no-build
```

## Wi-Fi and propagation contract

| Item | Value | Status | ns-3.47 mapping |
|---|---:|---|---|
| MAC/PHY standard | IEEE 802.11b | `PAPER` | `WIFI_STANDARD_80211b` |
| Data rate | 11 Mbps | `PAPER` | `DsssRate11Mbps` |
| Rate control | Fixed | `PORT` | `ConstantRateWifiManager` for data and control |
| MAC mode | Ad hoc | `PAPER` | `AdhocWifiMac`; no AP or association |
| Antenna | Omnidirectional | `PAPER` | ns-3 isotropic antenna unless evidence requires another model |
| Channel | 2.4 GHz, channel 1 | `PORT` | Use 2.412 GHz explicitly; Two-Ray defaults to 5.15 GHz |
| Propagation | Two-ray ground | `PAPER` | `TwoRayGroundPropagationLossModel` |
| Propagation delay | Constant speed | `PORT` | `ConstantSpeedPropagationDelayModel` |
| Transmission range target | 250 m | `PAPER` | A behavior target, not a hard range model |
| Carrier-sense range target | 550 m | `PAPER` | A behavior target, not a hard range model |
| RTS/CTS | Enabled | `PAPER` | Start with `RtsCtsThreshold=0`; prove via trace/PCAP |
| Aggregation | Disabled/not applicable | `PORT` | Preserve non-HT 802.11b operation |

`RangePropagationLossModel` must not be added to force the paper's nominal
range. CATRA depends on the distinction between decodable reception and
carrier-sense-only energy, so the boundary must emerge from received power and
PHY thresholds.

## PHY calibration contract

The following values are intentionally not locked in Phase 0:

| Parameter | Status | Selection rule |
|---|---|---|
| `TxPowerStart`/`TxPowerEnd` | 16 dBm | `PORT`, calibrated |
| `RxSensitivity` | -87 dBm | `PORT`, calibrated |
| `CcaEdThreshold` | -87 dBm | `PORT`, calibrated |
| `CcaSensitivity` | -75 dBm | `PORT`, calibrated |
| `RxNoiseFigure` | 18 dB | `PORT`, calibrated |
| `HeightAboveZ` | 1.5 m | `PORT`, calibrated |
| `SystemLoss` | 1.0 | `PORT`, calibrated |
| `MinDistance` | 0.5 m | `PORT` | ns-3 default; irrelevant to the 200 m spacing |

The Phase 1 probe must record RX success and `WifiPhyStateHelper::State`. Its
acceptance matrix is:

| Distance | Decode | Observe carrier busy |
|---:|---:|---:|
| 200 m | Yes | Yes |
| 250 m | Yes | Yes |
| 400 m | No | Yes |
| 550 m | No | Yes |
| 600 m | No | No |

Packet loss alone does not prove carrier-sense-only behavior. The 400 m case
passes only when it produces CCA busy time without successful data reception.
The calibrated received powers reported by ns-3.47 are approximately
`-70.116`, `-72.874`, `-81.039`, `-86.571`, and `-88.082` dBm at 200, 250,
400, 550, and 600 m respectively. These values exclude zero-valued PHY gains.
The calibration uses channel 1 at 2.412 GHz.

### Phase 1 calibration record

The probe was built and executed in the persistent ns-3 Docker service with
`seed=1` and `run=1,2,3`. All runs produced the same classifications:

| Distance | MAC packets decoded | PHY behavior | Result |
|---:|---:|---|---|
| 200 m | 10/10 | RX and CCA busy | Pass: decode |
| 250 m | 10/10 | RX and CCA busy | Pass: decode boundary |
| 400 m | 0/10 | CCA busy only | Pass: carrier-sense-only |
| 550 m | 0/10 | CCA busy only | Pass: carrier-sense boundary |
| 600 m | 0/10 | No RX or CCA busy | Pass: no detection |

Reproduce the strict calibration check with:

```bash
docker compose exec -T ns3 ./ns3 build catra-phy-range-probe -j 2
docker compose exec -T ns3 ./ns3 run catra-phy-range-probe --no-build
```

The executable returns a nonzero status when any acceptance condition fails.
Its thresholds remain command-line parameters so alternative calibrations can
be evaluated without changing source code.

## IPv4 and routing contract

| Item | Value | Status | Notes |
|---|---|---|---|
| Network | `10.1.1.0/24` | `PORT` | One IPv4 address per Wi-Fi interface |
| Routing | Static | `PORT` | The paper does not name a routing protocol |
| Forward path | Next neighbor to the right | `PORT` | Install destination-specific `/32` routes to `R` |
| Reverse path | Next neighbor to the left | `PORT` | Required for TCP ACKs and control packets |
| Route metric | Deterministic and equal | `PORT` | No route discovery or convergence interval |

Destination-specific host routes are required even though all stations share
one subnet. They prevent a distant destination from being treated as a directly
reachable Wi-Fi neighbor. Phase 3 must prove both directions one hop at a time
before TCP is installed.

## Common TCP Tahoe contract

| Item | Value | Status | Notes |
|---|---:|---|---|
| Paper TCP | Tahoe | `PAPER` | Required by Scenario 1 |
| Scenario TCP | `Scenario1TcpTahoe` | `PORT` | Reno growth rules plus Tahoe one-MSS loss recovery; SACK disabled |
| Application | `BulkSendApplication` | `PORT` | Saturated transfer with `MaxBytes=0` |
| Receiver | Two `PacketSink` instances | `PORT` | Ports 5001 and 5002 identify the flows |
| Segment/application send size | 1024 bytes | `PORT` | Treat the paper's 1 KB as payload; verify with PCAP |
| Simulation stop | 300 s | `PAPER` | Stop time, not automatically active duration |
| Traffic start | 1 s | `PORT` | Both flows start at the same simulation time |
| Active duration | 299 s | `PORT` | Used as the goodput denominator |
| Initial congestion window | ns-3.47 default | `PORT` | Record the resolved value in run metadata |
| Receive/send buffers | ns-3.47 defaults | `PORT` | Record resolved values; do not silently tune them |

The local class is an explicit ns-3.47 port because this checkout has no native
`TcpTahoe`. It must be reported as a Tahoe-compatible port rather than the
authors' original NS-2 implementation.

## Queue contract

| Item | Value | Status | Notes |
|---|---:|---|---|
| Paper buffer | 100 packets | `PAPER` | The paper does not identify the exact NS-2 queue |
| Initial ns-3 mapping | `WifiMacQueue::MaxSize=100p` | `PORT` | Do not also create a second 100-packet IP queue |
| MAC queue lifetime | 500 ms | `PORT` | Preserve the ns-3.47 default initially and report expiry drops |

The 500 ms queue lifetime can affect a saturated multi-hop result. Therefore,
queue-full drops and lifetime-expiry drops must be reported separately. If the
authors' NS-2 source reveals a different queue semantics, this decision must be
revisited and the baseline rerun.

## Measurement and output contract

The receiving `PacketSink` byte counters are the primary source for end-to-end
goodput. FlowMonitor is a cross-check and supplies packet loss and delay data.

For active duration `T`:

```text
flow1_mbps = flow1_received_bytes * 8 / T / 1,000,000
flow2_mbps = flow2_received_bytes * 8 / T / 1,000,000
total_e2e_mbps = flow1_mbps + flow2_mbps
paper_total_approx_mbps = flow1_mbps + (n - 1) * flow2_mbps
jain = (flow1_mbps + flow2_mbps)^2
       / (2 * (flow1_mbps^2 + flow2_mbps^2))
```

`paper_total_approx_mbps` is the explicit name for the paper's Equation (17)
approximation. It must not be labeled as directly measured channel throughput.

Every CSV row must contain:

```text
mode,n,seed,run,tcp,active_s,
flow1_mbps,flow2_mbps,total_e2e_mbps,paper_total_approx_mbps,jain,
flow1_rx_bytes,flow2_rx_bytes,flow2_path,flow2_relay_nodes,
flow2_forwarded_tcp_data_packets
```

The Flow 2 forwarding columns are runtime evidence from the IPv4
`UnicastForward` trace, not a path inferred only from node positions. The log
also emits one `[BASELINE-FORWARD]` record per relay and a complete
`[BASELINE-PATH]` record.

Run metadata must additionally print the resolved PHY thresholds, transmit
power, antenna height, queue size/lifetime, TCP segment size, TCP buffers,
traffic interval, and ns-3 version.

## Reproducibility and tracing contract

- Set and report `RngSeedManager` seed and run values.
- Use a short smoke test before every 300 s run.
- Run multiple `RngRun` values for every value of `n`; one run is diagnostic,
  not evidence of a trend.
- Keep PCAP disabled by default because saturated 300 s runs can create large
  files.
- Enable PCAP for short validation runs to prove RTS/CTS/DATA/ACK and hop path.
- Trace IPv4 forwarding, MAC retries, queue drops, TCP retransmissions, and
  congestion window changes with independently selectable flags.
- Emit one machine-readable CSV row per run and keep verbose logs separate
  from CSV output.

## Baseline acceptance gates

Phase 5 is complete only when all of the following are demonstrated:

- The calibrated PHY passes the 200/400/600 m matrix.
- Topology roles and positions are correct for `n=3,4,5,6`.
- UDP follows exactly `n-1` forward hops and the reverse path works.
- PCAP from a short run shows RTS/CTS/DATA/ACK.
- Both saturated TCP flows transfer data simultaneously.
- Sink counters and FlowMonitor totals agree within explained header semantics.
- The CSV formulas are covered by deterministic checks.
- Repeated runs show the expected long-flow degradation as `n` increases.
- All deviations from the paper remain labeled `PORT`, `CALIBRATE`, or
  `UNRESOLVED` in this document.

## CATRA constants reserved for later phases

These values are recorded now but must not change baseline behavior:

| Item | Value | Status |
|---|---:|---|
| Estimation period (`EP`) | 2 s | `PAPER` |
| High threshold (`Highth`) | 1.05 | `PAPER` |
| Low threshold (`Lowth`) | 0.7 | `PAPER` |
| Paper CWmin | 32 slots | `PAPER`; ns-3 inclusive value `31` |
| Paper CWmax | 1024 slots | `PAPER`; ns-3 inclusive value `1023` |

ns-3 samples backoff with `GetInteger(0, cw)`, so its stored CW is an inclusive
upper bound. Therefore paper window cardinality `W` maps to ns-3 `cw=W-1`.
The standard 802.11b defaults are already `31/1023`, exactly representing the
paper's `32/1024`. CATRA formulas must operate on `W=cw+1` and convert back.
Measurement state is per station and per flow; it must not use a process-wide
singleton.

## Planned source ownership

```text
scratch/catra/catra-phy-range-probe.cc    Phase 1 calibration executable
scratch/scenario1/                         Common Scenario 1 simulation and Tahoe
contrib/active-time/model/measurement/     Common passive Tactive/RBR measurement
contrib/catra/model/mac/                   Optional MAC CW' decision
contrib/catra/model/tcp/                   Optional Algorithm 2 decision
```

Standalone experiment assembly stays in `scratch`. Reusable flow identity,
airtime measurement and station state live in `contrib/active-time`; optional
MAC and TCP controllers live in `contrib/catra`; scenario assembly remains
under `scratch/scenario1`.

## Phase 0 completion record

- Paper values and ns-3 port decisions are separated.
- Tahoe is mapped through the local compatible port; contention-window mapping remains explicit.
- PHY calibration inputs and observable acceptance criteria are explicit.
- Routing, queue, measurement, reproducibility, and output contracts are fixed.
- Phase 1 is implemented by `scratch/catra/catra-phy-range-probe.cc`.
