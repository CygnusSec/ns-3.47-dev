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

- Algorithm 1 is integrated into Scenario 1 through `measure-only` mode.
- The MAC controller calculates and logs `CW'`; it does not yet mutate `Txop`.
- The TCP controller implements and validates Algorithm 2 decisions; it is not
  yet connected to a live ns-3 TCP socket/recovery path.
- `baseline` does not instantiate estimators or call either controller.

These boundaries prevent a read-only experiment from being mistaken for live
CATRA control.
