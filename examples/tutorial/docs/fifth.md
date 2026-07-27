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

`CwndChange` prints simulated time and the new congestion window. `RxDrop`
prints the time at which the receiver device rejects a frame.

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
