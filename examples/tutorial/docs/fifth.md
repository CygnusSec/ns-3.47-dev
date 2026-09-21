# Fifth Tutorial: TCP Congestion Tracing

Source: `examples/tutorial/fifth.cc`

Related reusable application: [TutorialApp](tutorial-app.md)

## Purpose

This example creates a lossy TCP flow and observes how TCP NewReno changes its
congestion window. It demonstrates direct socket creation, socket trace
sources, receive-drop tracing, and a custom traffic application.

## Topology

```text
n0 / 10.1.1.1  --------  n1 / 10.1.1.2:8080
TCP sender        5 Mbps   TCP PacketSink
                  2 ms
```

The receiver device has a `RateErrorModel` with error rate `0.00001`.

## Why the socket is created manually

The example must connect to `CongestionWindow` before traffic begins. A helper
application may create and hide its socket only at start time. Creating the
socket explicitly makes the trace source available during configuration.

## TCP configuration

The example selects:

```text
Congestion control : TcpNewReno
Initial cwnd       : 1 segment
Recovery algorithm: TcpClassicRecovery
```

`TutorialApp` attempts to send 1000 packets of 1040 bytes at 1 Mbps.

## Trace callbacks

`CwndChange` prints the simulated time, active phase, AIMD action, congestion
window in bytes and MSS units, delta or reduction ratio, and the current
`ssthresh`. The labels distinguish:

- `SLOW_START / EXPONENTIAL_INCREASE`: approximately doubles `cwnd` per RTT.
- `CONGESTION_AVOIDANCE / ADDITIVE_INCREASE`: approximately adds one MSS per RTT.
- `LOSS_RECOVERY / MULTIPLICATIVE_DECREASE`: reduces the sending window after loss.

`SsThreshChange` prints NewReno's loss response and threshold rule. `RxDrop`
prints the time at which the receiver device rejects a frame.

Run the focused congestion-control trace with:

```bash
docker compose exec -T ns3 ./ns3 run \
  "fifth --printAttributes=false --detailedLog=false"
```

Set `--detailedLog=true` only when the additional RTT, RTO, bytes-in-flight,
connection-state, and retransmission traces are needed.

Every run also creates `fifth-cwnd.csv`. Each row represents one congestion
window change and contains:

```text
time_s,old_cwnd_bytes,new_cwnd_bytes,old_cwnd_mss,new_cwnd_mss,ssthresh_bytes,ssthresh_mss,phase,action
```

For plotting, use `time_s` as the x-axis and `new_cwnd_mss` (or
`new_cwnd_bytes`) as the y-axis. Use `phase` to color Slow Start, Congestion
Avoidance, and Loss Recovery. The output path can be changed with, for example,
`--cwndCsvFile=results/newreno-cwnd.csv`.

Install `plotext` once, then render the CSV directly in the terminal:

```bash
python3 -m pip install 'plotext>=6.1'
python3 utils/plot-tcp-congestion.py fifth-cwnd.csv
```

The high-contrast dark chart uses a white line for the complete `cwnd`
evolution, green `S` markers for Slow Start, cyan `A` markers for Additive
Increase, and red `M` markers for Multiplicative Decrease. The yellow
`ssthresh` line begins after the first loss response; the initial unlimited
`UINT32_MAX` threshold is intentionally hidden so it does not flatten the
useful y-axis range. The terminal chart size can be changed with `--width` and
`--height`.

The common debug helper additionally prints topology and can trace every IPv4
TCP send, forward, and delivery event:

```bash
docker compose exec ns3 ./ns3 run \
  "fifth --printAttributes=false --tracePackets=true"
```

Packet tracing defaults to false because a 1000-packet TCP flow produces a
large amount of output.

## Practical use

Use this tutorial to study slow start, congestion avoidance, retransmission,
loss recovery, congestion-window reductions, and total bytes delivered to a
TCP sink.
