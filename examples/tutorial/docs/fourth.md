# Fourth Tutorial: Trace Sources and Callbacks

Source: `examples/tutorial/fourth.cc`

## Purpose

This example introduces ns-3 instrumentation. A model publishes an internal
state change as a trace source, while external callbacks decide how to print,
store, test, or visualize the event.

## The model

`MyObject` inherits `ns3::Object` and registers runtime metadata through
`GetTypeId`. Its traced state is:

```cpp
TracedValue<int32_t> m_myInt;
```

The member is published under the external name `MyInteger`:

```cpp
.AddTraceSource(
    "MyInteger",
    "An integer value to trace.",
    MakeTraceSourceAccessor(&MyObject::m_myInt),
    "ns3::TracedValueCallback::Int32")
```

The C++ member name and public trace-source name do not need to match.

## Connecting the observer

```cpp
myObject->TraceConnectWithoutContext(
    "MyInteger",
    MakeCallback(&IntTrace));
```

The callback signature receives the state transition:

```cpp
void IntTrace(int32_t oldValue, int32_t newValue);
```

Assigning `1234` immediately invokes `IntTrace(0, 1234)`.

## Why no simulator event loop is needed

The assignment is synchronous and no event is scheduled. `Simulator::Run()` is
only required when work has been placed in the event queue.

## Applications

The same pattern is used for TCP congestion windows, queue length, packet
drops, IPv4 forwarding, Wi-Fi events, and mobility course changes. It separates
simulation behavior from debugging and measurement code.

## Run

```bash
docker compose exec ns3 ./ns3 run fourth
```

The output shows the object type, base type, trace source, initial value,
callback invocation, delta, and final value.
