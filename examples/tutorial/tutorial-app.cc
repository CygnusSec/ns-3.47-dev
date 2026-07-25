/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Import the declaration of TutorialApp implemented in this file.
#include "tutorial-app.h"

// Import application-related definitions used by the implementation.
#include "ns3/applications-module.h"

// Avoid repeating ns3:: on every ns-3 type.
using namespace ns3;

// Initialize every member explicitly so a newly created application is safe but inactive.
TutorialApp::TutorialApp()
    : m_socket(nullptr),
      m_peer(),
      m_packetSize(0),
      m_nPackets(0),
      m_dataRate(0),
      m_sendEvent(),
      m_running(false),
      m_packetsSent(0)
{
}

// Break the reference to the socket when the application object is destroyed.
TutorialApp::~TutorialApp()
{
    m_socket = nullptr;
}

/* static */
TypeId
TutorialApp::GetTypeId()
{
    // Register the class once in the ns-3 runtime type system.
    static TypeId tid = TypeId("TutorialApp")
                            // Declare Application as the base type.
                            .SetParent<Application>()
                            // Group the class with the tutorial examples.
                            .SetGroupName("Tutorial")
                            // Make the class constructible through CreateObject.
                            .AddConstructor<TutorialApp>();

    // Return the cached type metadata.
    return tid;
}

// Store all parameters required to generate the traffic stream.
void
TutorialApp::Setup(Ptr<Socket> socket,
                   Address address,
                   uint32_t packetSize,
                   uint32_t nPackets,
                   DataRate dataRate)
{
    // Retain the socket that was created and traced by the calling example.
    m_socket = socket;
    // Retain the destination address and port.
    m_peer = address;
    // Save the payload size used by Create<Packet>().
    m_packetSize = packetSize;
    // Save the maximum number of transmissions.
    m_nPackets = nPackets;
    // Save the rate used when calculating the inter-packet interval.
    m_dataRate = dataRate;
}

// Activate the application when its scheduled start event executes.
void
TutorialApp::StartApplication()
{
    // Permit SendPacket() to schedule subsequent transmissions.
    m_running = true;
    // Reset the counter so each application run starts from packet zero.
    m_packetsSent = 0;
    // Allocate a suitable local endpoint for the socket.
    m_socket->Bind();
    // Associate the socket with the configured remote endpoint.
    m_socket->Connect(m_peer);
    // Send the first packet immediately at application start time.
    SendPacket();
}

// Stop future traffic and release socket resources.
void
TutorialApp::StopApplication()
{
    // Prevent ScheduleTx() from creating another event.
    m_running = false;

    // Cancel the pending transmission, if one has already been scheduled.
    if (m_sendEvent.IsPending())
    {
        Simulator::Cancel(m_sendEvent);
    }

    // Close the socket only when Setup() supplied a valid socket.
    if (m_socket)
    {
        m_socket->Close();
    }
}

// Create and transmit one packet, then arrange the next transmission if needed.
void
TutorialApp::SendPacket()
{
    // Allocate a packet containing m_packetSize bytes of application payload.
    Ptr<Packet> packet = Create<Packet>(m_packetSize);

    // Pass the packet to the connected transport socket.
    m_socket->Send(packet);

    // Increment the counter and continue until the requested total is reached.
    if (++m_packetsSent < m_nPackets)
    {
        ScheduleTx();
    }
}

// Convert the desired data rate into an event delay and schedule SendPacket().
void
TutorialApp::ScheduleTx()
{
    // Do not schedule work after StopApplication() has run.
    if (m_running)
    {
        // Transmission interval = packet bits / configured bits per second.
        Time tNext(Seconds(m_packetSize * 8 / static_cast<double>(m_dataRate.GetBitRate())));

        // Store the EventId so StopApplication() can cancel this event later.
        m_sendEvent = Simulator::Schedule(tNext, &TutorialApp::SendPacket, this);
    }
}
