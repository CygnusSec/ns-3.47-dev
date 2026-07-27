# C++ Tutorial Guides

These guides explain every C++ source file in `examples/tutorial`.

| Source | Guide | Main topic |
|---|---|---|
| `hello-simulator.cc` | [Hello Simulator](hello-simulator.md) | Logging and the minimal ns-3 program |
| `first.cc` | [First](first.md) | Two-node UDP Echo |
| `second.cc` | [Second](second.md) | Routed point-to-point and CSMA networks |
| `third.cc` | [Third](third.md) | Wi-Fi, mobility, routing, P2P, and CSMA |
| `fourth.cc` | [Fourth](fourth.md) | Trace sources and callbacks |
| `fifth.cc` | [Fifth](fifth.md) | TCP congestion-window tracing |
| `sixth.cc` | [Sixth](sixth.md) | TCP trace files and PCAP output |
| `seventh.cc` | [Seventh](seventh.md) | Probes, statistics, plots, IPv4, and IPv6 |
| `tutorial-app.cc` | [TutorialApp](tutorial-app.md) | Reusable rate-controlled traffic application |

## Running in Docker

Build one example:

```bash
docker compose exec ns3 ./ns3 build third -j 2
```

Run it without rebuilding:

```bash
docker compose exec ns3 ./ns3 run third --no-build
```

Show its supported arguments:

```bash
docker compose exec ns3 ./ns3 run "third --help"
```

The network tutorials link the project-wide `debug-tools` module. Its topology
report discovers nodes, applications, devices, channels, IP addresses, routing
protocols, mobility models, positions, velocities, and readable ns-3
attributes.
