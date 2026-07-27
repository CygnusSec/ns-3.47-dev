# Second Tutorial: Routed P2P and CSMA

Source: `examples/tutorial/second.cc`

## Purpose

This example adds a shared CSMA LAN and an IPv4 router to the first tutorial.
It demonstrates multiple subnets, global route calculation, forwarding, and a
configurable server node.

## Default topology

```text
P2P 10.1.1.0/24             CSMA 10.1.2.0/24

n0/client -------- n1/router -------- n2
10.1.1.1           10.1.1.2  \------- n3
                    10.1.2.1   \------ n4/server
```

`n1` is not duplicated. The same node is referenced by both `p2pNodes` and
`csmaNodes`, and it owns one interface in each subnet.

## Routing and packet path

`Ipv4GlobalRoutingHelper::PopulateRoutingTables()` calculates routes after all
interfaces are assigned. With the default server `n4`, the request path is:

```text
n0 SEND -> n1 FORWARD -> n4 DELIVER
```

The reply follows the reverse path. TTL decreases at `n1` because forwarding
crosses one router.

## Selecting the server

Zero means the final CSMA host. A specific global node number can also be used:

```bash
docker compose exec ns3 ./ns3 run \
  "second --serverNode=2 --printRoutes=false"
```

Valid server nodes are the extra CSMA hosts, from `n2` through `n(nCsma+1)`.

## Debug options

```text
--printRoutes=true|false
--tracePackets=true|false
--verbose=true|false
--nCsma=N
--serverNode=N
```

The common debug helper discovers topology and prints routing tables and IPv4
send, forward, and deliver events.

## Practical use

Use this example to understand gateways, interface ownership, shared media,
subnet boundaries, routing-table entries, and the difference between a host
that delivers a packet and a router that forwards it.
