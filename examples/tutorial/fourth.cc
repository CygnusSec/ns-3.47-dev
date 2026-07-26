/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Import ns-3's base Object type and runtime type-information system.
#include "ns3/object.h"
// Import the discrete-event simulator API.
#include "ns3/simulator.h"
// Import helpers that expose an object member as a configurable trace source.
#include "ns3/trace-source-accessor.h"
// Import TracedValue, a value wrapper that invokes callbacks when its value changes.
#include "ns3/traced-value.h"
// Import the unsigned-integer attribute helper types.
#include "ns3/uinteger.h"

// Import standard output streams for printing the observed value change.
#include <iostream>

// Make ns-3 names available without an ns3:: prefix.
using namespace ns3;

/**
 * Tutorial 4 - a simple Object to show how to hook a trace.
 */
class MyObject : public Object
{
  public:
    /**
     * Register this type.
     * @return The TypeId.
     */
    static TypeId GetTypeId()
    {
        // Construct this metadata once and reuse it on every later call.
        static TypeId tid = TypeId("MyObject")
                                // Declare Object as the base class in the ns-3 type system.
                                .SetParent<Object>()
                                // Place the type in the Tutorial documentation group.
                                .SetGroupName("Tutorial")
                                // Allow CreateObject<MyObject>() to construct this type.
                                .AddConstructor<MyObject>()
                                // Publish m_myInt under the trace-source name "MyInteger".
                                .AddTraceSource("MyInteger",
                                                "An integer value to trace.",
                                                MakeTraceSourceAccessor(&MyObject::m_myInt),
                                                "ns3::TracedValueCallback::Int32");

        // Return the registered metadata to the caller.
        return tid;
    }

    // The default constructor has no additional initialization work.
    MyObject()
    {
    }

    // Assigning a new value invokes every callback connected to this traced value.
    TracedValue<int32_t> m_myInt; //!< The traced value.
};

// This callback receives the value before and after every traced assignment.
void
IntTrace(int32_t oldValue, int32_t newValue)
{
    // Print the state transition observed through the trace source.
    std::cout << "[TRACE] source=MyInteger old=" << oldValue << " new=" << newValue
              << " delta=" << newValue - oldValue << std::endl;
}

// Build one object, connect its trace source, and trigger a value change.
int
main(int argc, char* argv[])
{
    // Allocate MyObject using ns-3 reference-counted object ownership.
    Ptr<MyObject> myObject = CreateObject<MyObject>();

    std::cout << "================ Fourth Tutorial Trace Model ================\n"
              << "Object model : " << myObject->GetInstanceTypeId().GetName() << "\n"
              << "Base model   : " << myObject->GetInstanceTypeId().GetParent().GetName() << "\n"
              << "Trace source : MyInteger\n"
              << "Value type   : int32_t\n"
              << "Initial value: " << myObject->m_myInt.Get() << "\n"
              << "Callback     : IntTrace(oldValue, newValue)\n";

    // Connect IntTrace to MyInteger without adding a configuration-path argument.
    myObject->TraceConnectWithoutContext("MyInteger", MakeCallback(&IntTrace));

    // Change the traced value, which immediately calls IntTrace(oldValue, 1234).
    myObject->m_myInt = 1234;

    std::cout << "Final value  : " << myObject->m_myInt.Get() << "\n"
              << "Result       : callback connected and invoked synchronously\n"
              << "=============================================================\n";

    // End the process successfully; no event loop is needed for this synchronous trace.
    return 0;
}
