# Seventh Tutorial: Probes, Statistics, IPv4, and IPv6

Source: `examples/tutorial/seventh.cc`

Related reusable application: [TutorialApp](tutorial-app.md)

## Purpose

This tutorial extends the lossy TCP experiment with the ns-3 statistics
framework. It demonstrates probes, aggregators, formatted files, Gnuplot
generation, and switching the same experiment between IPv4 and IPv6.

## Address-family selection

IPv4 is the default:

```text
10.1.1.0/24
probe: ns3::Ipv4PacketProbe
path : /NodeList/*/$ns3::Ipv4L3Protocol/Tx
```

With `--useIpv6=true`:

```text
2001:0:f00d:cafe::/64
probe: ns3::Ipv6PacketProbe
path : /NodeList/*/$ns3::Ipv6L3Protocol/Tx
```

The sink address, wildcard listener address, probe type, and trace path all
change together.

## Statistics pipeline

```text
IPv4/IPv6 Tx trace
       ↓
PacketProbe converts packet events to OutputBytes
       ├─ GnuplotHelper -> plot data and script
       └─ FileHelper    -> formatted text data
```

The plot shows packet byte count against simulation time. The formatted output
is useful for scripts, spreadsheets, and regression comparisons.

The example also writes:

- `seventh.cwnd`: congestion-window changes.
- `seventh.pcap`: receiver-side dropped packets.
- `seventh-packet-byte-count*`: statistics and plotting files.

## Run

IPv4:

```bash
docker compose exec ns3 ./ns3 run \
  "seventh --printAttributes=false"
```

IPv6:

```bash
docker compose exec ns3 ./ns3 run \
  "seventh --useIpv6=true --printAttributes=false"
```

Enable the matching packet-flow tracer:

```bash
docker compose exec ns3 ./ns3 run \
  "seventh --useIpv6=true --tracePackets=true --printAttributes=false"
```

## Practical use

Use this tutorial to build reproducible metrics pipelines, compare IPv4 and
IPv6 behavior, generate plots automatically, and separate data collection
from simulation models.
