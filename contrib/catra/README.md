# CATRA shared module

This ns-3 module contains CATRA logic that is independent of a concrete
scenario. A scenario opts in by linking `${libcatra}` and creating the required
measurement/controller objects. Merely running a baseline mode does not create
CATRA state or alter MAC/TCP behavior.

## Ownership

```text
model/measurement/  Algorithm 1 packet parsing, MAC ACK correlation and active time
model/mac/          Equation (5) CW' decision and ns-3 CW representation mapping
model/tcp/          Algorithm 2 side-effect-free TCP cwnd/delay decision
```

Scenario-specific topology, route-derived flow counts, applications and output
remain under `scratch/catra/scenario1`.

## Current integration boundary

- Algorithm 1 measurement is independently selectable with `--measure=on/off`.
- `--catra=on` applies `CW'` as the adaptive `Txop` CWmin while retaining the
  standard CWmax, so native DCF/BEB remains active.
- The TCP controller implements and validates Algorithm 2 decisions; it is not
  yet connected to a live ns-3 TCP socket/recovery path.
- `--catra=off --measure=on` provides a read-only decision preview.

These boundaries prevent a read-only experiment from being mistaken for live
CATRA control.

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
