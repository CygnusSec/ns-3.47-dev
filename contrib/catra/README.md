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
