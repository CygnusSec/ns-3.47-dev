# First Tutorial: Two-Node UDP Echo

Source: `examples/tutorial/first.cc`

## Purpose

This example introduces the standard ns-3 workflow:

1. Create nodes.
2. Create devices and a channel.
3. Install protocol stacks.
4. Assign addresses.
5. Install applications.
6. Run and destroy the simulator.

## Topology

```text
10.1.1.0/24

n0 / 10.1.1.1  --------  n1 / 10.1.1.2
UDP Echo client   5 Mbps   UDP Echo server :9
                  2 ms
```

`PointToPointHelper` installs one `PointToPointNetDevice` on each node and
connects them through one `PointToPointChannel`.

## Applications and timing

The server listens on UDP port 9 from 1 to 10 seconds. The client starts at 2
seconds and sends one 1024-byte payload. Starting the server first prevents the
request from arriving before a socket is listening.

The packet does not pass through a router:

```text
n0 SEND -> n1 DELIVER
n1 SEND -> n0 DELIVER
```

## Debug output

`SimulationDebugHelper::PrintTopology` prints both nodes, applications,
devices, MAC addresses, IPv4/IPv6 interfaces, channel endpoints, data rate, and
delay. IPv4 flow tracing prints each send and delivery action.

Disable verbose model attributes or packet tracing:

```bash
docker compose exec ns3 ./ns3 run \
  "first --printAttributes=false --tracePackets=false"
```

## Practical use

This is the baseline for validating addressing, application timing, direct
link delivery, UDP request/reply behavior, and breakpoints in client/server
applications.
