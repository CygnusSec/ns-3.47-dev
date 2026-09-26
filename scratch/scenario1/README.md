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

`CMakeLists.txt` always links the common contributed `active-time` measurement
module and registers the `scenario1` target. If the contributed `catra` target
exists, it additionally links `${libcatra}` and defines
`NS3_CATRA_MODULE_ENABLED`.

## Module behavior

- CATRA disabled: run the common Scenario 1 TCP Tahoe simulation, calculate
  `Tactive/RBR`, and write station-state output without calculating or applying
  `CW'`; output prefix is `no-catra-`.
- CATRA enabled: run the same measurement pipeline, then let the CATRA
  controller calculate and apply adaptive `CW'`; output prefix is `catra-`.

There is no runtime CATRA switch and there are no separate baseline/CATRA
Scenario 1 source trees.

## Prepare result CSVs for plotting

After running the same experiment once with CATRA enabled and once with CATRA
disabled, combine and derive plot-ready metrics with:

```bash
python3 scripts/catra/extract-scenario1-channel-access.py \
  --input-dir results/scenario1 \
  --output-dir results/scenario1/plot-data \
  --traffic-profile paper
```

The extractor pairs inputs by traffic profile and filename experiment suffix,
for example `n3-links100-200m-run1`. It refuses unpaired CATRA/no-CATRA
station-state results by default. Its outputs include channel-access time
series, per-station summaries, paired deltas, CW/backoff aggregates per EP,
per-hop MAC time series, and combined throughput data. Raw simulation CSVs are
read-only inputs and are never modified.
