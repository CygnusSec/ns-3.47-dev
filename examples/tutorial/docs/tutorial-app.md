# TutorialApp: Reusable Rate-Controlled Sender

Sources:

- `examples/tutorial/tutorial-app.h`
- `examples/tutorial/tutorial-app.cc`

Used by `fifth.cc`, `sixth.cc`, and `seventh.cc`.

## Purpose

`TutorialApp` is a small custom `Application` that sends a configured number of
fixed-size packets through a socket at a configured application data rate. It
exists so the tutorial owns the sender socket and can connect TCP trace
callbacks before the application starts.

## Configuration

`Setup` receives:

```text
socket        pre-created TCP socket
peer          destination address and port
packetSize    bytes per application packet
nPackets      maximum packet count
dataRate      target application bit rate
```

For the TCP tutorials:

```text
packetSize = 1040 bytes
nPackets   = 1000
dataRate   = 1 Mbps
```

## Lifecycle

`StartApplication` marks the application as running, resets the packet count,
binds and connects the socket, and starts transmission.

`SendPacket` creates one packet, sends it, increments the count, and schedules
the next transmission if work remains.

`StopApplication` cancels the pending send event and closes the socket.

## Rate calculation

The interval between packets is derived from:

```text
interval = packetSize * 8 / dataRate
```

For 1040-byte packets at 1 Mbps:

```text
1040 * 8 / 1,000,000 = 0.00832 seconds
```

This is an application pacing interval, not the P2P serialization time. TCP,
the device queue, link capacity, propagation delay, and retransmissions still
determine actual network delivery.

## Why it is not an executable

`tutorial-app.cc` has no `main()`. CMake compiles it into each tutorial target
that lists it in `SOURCE_FILES`. Run `fifth`, `sixth`, or `seventh` to exercise
the class.

## Practical use

The class demonstrates how to implement an ns-3 application lifecycle, retain
an externally created socket, pace traffic with scheduled events, and expose a
simple reusable sender to multiple experiments.
