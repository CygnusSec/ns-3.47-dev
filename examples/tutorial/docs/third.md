# Third Tutorial: Wi-Fi, Mobility, P2P, and CSMA

Source: `examples/tutorial/third.cc`

## Purpose

This example combines three link technologies and sends one routed UDP Echo
exchange from a mobile Wi-Fi station to a wired CSMA server.

## Default topology

```text
Wi-Fi 10.1.3.0/24       P2P 10.1.1.0/24       CSMA 10.1.2.0/24

n5 ─┐
n6 ─┼─ n0/AP+router ─────── n1/router ─┬─ n2
n7 ─┘                                  ├─ n3
client                                 └─ n4/server:9
```

`n0` owns a Wi-Fi AP interface and a point-to-point interface. `n1` owns a
point-to-point interface and a CSMA interface. These multi-interface nodes
route traffic between subnets.

## Wi-Fi configuration

Stations use `StaWifiMac`; `n0` uses `ApWifiMac`. All devices share SSID
`ns-3-ssid` and a `YansWifiChannel`. Active probing is disabled, so stations
discover the AP from beacon traffic.

## Mobility

Stations begin on a grid:

```text
n5=(0,0,0), n6=(5,0,0), n7=(10,0,0), AP n0=(0,10,0)
```

Stations use `RandomWalk2dMobilityModel` inside `[-50,50] x [-50,50]`. The AP
uses `ConstantPositionMobilityModel`. The common topology report prints the
mobility model, initial position, velocity, and readable bounds attributes.

## Packet path

```text
n7 SEND -> n0 FORWARD -> n1 FORWARD -> n4 DELIVER
n4 SEND -> n1 FORWARD -> n0 FORWARD -> n7 DELIVER
```

TTL decreases twice because the packet crosses two routers.

## Useful commands

Compact output:

```bash
docker compose exec ns3 ./ns3 run \
  "third --printAttributes=false --printRoutes=false --verbose=false"
```

Change topology size:

```bash
docker compose exec ns3 ./ns3 run \
  "third --nWifi=5 --nCsma=4 --printAttributes=false"
```

Enable PCAP capture on representative Wi-Fi, P2P, and CSMA devices:

```bash
docker compose exec ns3 ./ns3 run "third --tracing=true"
```

## Practical use

Use this example for heterogeneous networks, wireless association, mobility,
multi-interface routers, end-to-end routing, PCAP inspection, and correlating
packet paths with node position.
