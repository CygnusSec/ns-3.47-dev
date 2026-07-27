# Hello Simulator

Source: `examples/tutorial/hello-simulator.cc`

## Purpose

This is the smallest C++ ns-3 executable. It demonstrates how to include the
core module, define a log component, and print an unconditional message. It
does not create nodes, schedule events, or run the simulator.

## Main concepts

`#include "ns3/core-module.h"` imports the core runtime, time, events, command
line support, attributes, and logging facilities.

`NS_LOG_COMPONENT_DEFINE("HelloSimulator")` registers a component name. A real
model can use that name with `NS_LOG_INFO`, `NS_LOG_DEBUG`, and other filtered
log levels.

`NS_LOG_UNCOND` always prints:

```cpp
NS_LOG_UNCOND("Hello Simulator");
```

Unlike filtered logging, it does not require enabling a component through
`NS_LOG`.

## Why there is no `Simulator::Run()`

The program schedules no events. Both output statements execute immediately in
normal C++ control flow, so an event loop would do nothing.

## Run

```bash
docker compose exec ns3 ./ns3 run hello-simulator
```

Expected result:

```text
Hello Simulator
Runtime summary: core module loaded; no nodes, devices, channels, or events
```

## Practical use

Use this example to verify that the project builds, the container starts, and
the ns-3 core library can be loaded before debugging a larger simulation.
