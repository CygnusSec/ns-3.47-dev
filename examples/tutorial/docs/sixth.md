# Sixth Tutorial: Persisting TCP Traces

Source: `examples/tutorial/sixth.cc`

Related reusable application: [TutorialApp](tutorial-app.md)

## Purpose

This tutorial uses the same two-node lossy TCP experiment as the fifth
tutorial, but writes trace data to files for offline analysis.

## Generated data

`AsciiTraceHelper::CreateFileStream("sixth.cwnd")` creates a text stream.
Every congestion-window callback writes:

```text
simulation-time    old-cwnd    new-cwnd
```

`PcapHelper::CreateFile("sixth.pcap", ...)` creates a PCAP file. The `RxDrop`
callback records packets rejected by the receiver-side error model.

These files answer different questions:

- `sixth.cwnd`: how TCP control state changes over time.
- `sixth.pcap`: which packet bytes were dropped and when.

## Topology and traffic

```text
n0/10.1.1.1 -> n1/10.1.1.2:8080
1000 packets x 1040 bytes at 1 Mbps
P2P: 5 Mbps, 2 ms
Receive error rate: 0.00001
```

The final summary obtains `PacketSink::GetTotalRx()` to report how many
application bytes reached the receiver.

## Run

```bash
docker compose exec ns3 ./ns3 run \
  "sixth --printAttributes=false"
```

Deep packet-level tracing is optional:

```bash
docker compose exec ns3 ./ns3 run \
  "sixth --printAttributes=false --tracePackets=true"
```

## Practical use

Use this tutorial when measurements must survive after the process exits, be
plotted with Python or Gnuplot, compared between simulation runs, or inspected
in Wireshark.
