# CATRA shared module

This ns-3 module contains CATRA logic that is independent of a concrete
scenario. A scenario opts in by linking `${libcatra}` and creating the required
measurement/controller objects. Scenario 1 itself remains available as a TCP
Tahoe baseline when this module is disabled.

## Ownership

```text
model/measurement/  Algorithm 1 packet parsing, MAC ACK correlation and active time
model/mac/          Equation (5) CW' decision and ns-3 CW representation mapping
model/tcp/          Algorithm 2 side-effect-free TCP cwnd/delay decision
```

Scenario-specific topology, route-derived flow counts, applications and output
remain under `scratch/catra/scenario1`.

## Current integration boundary

- With the CATRA module enabled, Scenario 1 TCP phases run Algorithm 1 because
  it supplies the RBR input for CATRA CW control, then apply `CW'` as adaptive
  `Txop` CWmin while retaining the standard CWmax so native DCF/BEB remains
  active.
- With the CATRA module disabled, the same Scenario 1 target runs its TCP Tahoe
  flows without Algorithm 1 measurement and without changing CWmin.
- The TCP controller implements and validates Algorithm 2 decisions; it is not
  yet connected to a live ns-3 TCP socket/recovery path.
- CATRA availability is a configure-time ns-3 module decision, not a runtime
  `--catra` switch.

There is no runtime `--catra` flag. Enabling or disabling CATRA is an ns-3
configure-time module choice.

## Enable or disable the ns-3 module

Enable CATRA together with the normal ns-3 module set before building
Scenario 1:

```bash
./ns3 configure --enable-modules= --disable-modules=
./ns3 build catra-scenario1
```

An empty `--enable-modules` list means that ns-3 does not apply an enable
whitelist. CATRA is then built normally with the other modules. Do not use
`--enable-modules=catra` for Scenario 1: in ns-3 this means "build only CATRA
and its declared module dependencies", so scenario-only dependencies such as
`applications`, `flow-monitor` and `debug-tools` are excluded.

For an intentionally reduced build, list every Scenario 1 dependency:

```bash
./ns3 configure \
  --enable-modules=catra,applications,debug-tools,flow-monitor,internet,mobility,network,propagation,wifi \
  --disable-modules=
```

Disable CATRA with:

```bash
./ns3 configure --enable-modules= --disable-modules=catra
./ns3 build catra-scenario1
```

The enable and disable commands explicitly clear the opposite cached module
list. This matters when switching an existing CMake build directory between
enabled and disabled states.

The `catra-scenario1` executable is registered in both configurations. Without
the module it reports `catra_module=disabled`, runs the baseline TCP traffic,
and still produces throughput, CW/backoff and per-hop MAC output; it does not
create the CATRA station-state CSV. With the module it reports
`catra_module=enabled` and automatically adds Algorithm 1 plus CW control.
The module-only `catra-active-time-probe` and `catra-tcp-controller-probe`
targets are not registered while CATRA is disabled.

The same runner works in either configuration:

```bash
TRAFFIC_PROFILE=paper SIM_TIME=300 MAC_HOP_INTERVAL=1 \
ADJACENT_DISTANCE_SETS="100,200" \
./scripts/catra/run-algorithm1-scenario1.sh
```

The runner reads the configured module targets from `./ns3 show targets`.
CATRA-enabled output files use the `catra-` prefix; CATRA-disabled baseline
files use `baseline-`. This keeps the two result sets separate without adding
a runtime CATRA flag.

Scenario 1 uses `--trafficProfile=paper` for the paper's two TCP flows. The
separate `--trafficProfile=tcp-stress` profile adds a saturated CatraTcpTahoe
flow from R to S1 and includes it in the Algorithm 1 flow counts. This stress
flow is experiment traffic rather than part of the paper scenario.
`--traceCw=true` records the native ns-3 `CwTrace` and
`BackoffTrace` events, including BEB increases, success resets, random backoff
slots, fixed slot time, CATRA EP updates, and the resulting backoff duration.

## Scenario 1 per-hop MAC time series

`--measureMacHops=true` records transmitted load from `MonitorSnifferTx` in
fixed intervals selected by `--macHopInterval` (one second by default). The
output path is selected with `--macHopCsv`.

For every adjacent hop and interval, the CSV contains three rows: one for each
physical direction (`n0->n1` and `n1->n0`, for example) and a `both` row that
sums them. The counters include:

- TCP DATA and pure TCP ACK MAC-DATA frames for Flow1, Flow2 and TCP stress;
- every retransmitted MAC-DATA attempt, identified by the 802.11 Retry bit;
- RTS, CTS and normal MAC ACK frames;
- total transmitted MAC bytes, `total_mac_tx_mbps`, transmit airtime and PHY
  airtime ratio.

This metric is transmitted MAC load, not application goodput: retransmissions
and control frames intentionally increase it. The older IP receive-byte metric
is retained only as `BASELINE-IP-HOP-GOODPUT` for comparison. In paper profile,
the final hop's `both` row is the combined real link load of Flow1, Flow2 and
their reverse TCP ACK traffic.
