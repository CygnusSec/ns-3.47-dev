# Scenario 1

This directory contains one common Scenario 1 simulation. Its topology,
routes, Flow1/Flow2 traffic, TCP Tahoe implementation, DCF tracing, forwarding
validation and per-hop MAC measurements do not depend on CATRA.

## Source ownership

```text
scenario1-main.cc                    CLI and orchestration
scenario1-traffic.cc/.h             applications, forwarding and throughput
scenario1-radio.cc/.h               radio relationships and flow-count inputs
scenario1-cw-trace.cc/.h            DCF/BEB/CATRA-update event provenance
scenario1-mac-hop-measurement.cc/.h  bidirectional per-hop MAC time series
tcp-tahoe.cc/.h                      common Tahoe-compatible TCP
```

`CMakeLists.txt` always registers the `scenario1` target. If the contributed
`catra` target exists, it additionally links `${libcatra}` and defines
`NS3_CATRA_MODULE_ENABLED`.

## Module behavior

- CATRA disabled: run the common Scenario 1 TCP Tahoe simulation without
  Algorithm 1 or CW' updates; output prefix is `no-catra-`.
- CATRA enabled: run the same simulation and additionally create Algorithm 1
  estimators, station-state output and adaptive CW' control; output prefix is
  `catra-`.

There is no runtime CATRA switch and there are no separate baseline/CATRA
Scenario 1 source trees.
