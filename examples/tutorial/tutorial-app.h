/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Prevent this header from being included more than once per translation unit.
#ifndef TUTORIAL_APP_H
#define TUTORIAL_APP_H

// Import time, data-rate, TypeId, and other core definitions.
#include "ns3/core-module.h"
// Import Socket and Internet address support.
#include "ns3/internet-module.h"
// Import Application, Packet, and network base types.
#include "ns3/network-module.h"

// Keep TutorialApp in the same namespace as the rest of ns-3.
namespace ns3
{

// Declare Application before the full class definition is needed.
class Application;

/**
 * Tutorial - a simple Application sending packets.
 */
class TutorialApp : public Application
{
  public:
    // Initialize a new, inactive traffic generator.
    TutorialApp();

    // Release application-owned references when the object is destroyed.
    ~TutorialApp() override;

    /**
     * Register this type.
     * @return The TypeId.
     */
    static TypeId GetTypeId();

    /**
     * Setup the socket.
     * @param socket The socket.
     * @param address The destination address.
     * @param packetSize The packet size to transmit.
     * @param nPackets The number of packets to transmit.
     * @param dataRate the data rate to use.
     */
    void Setup(Ptr<Socket> socket,
               Address address,
               uint32_t packetSize,
               uint32_t nPackets,
               DataRate dataRate);

  private:
    // Called automatically by Application at the configured start time.
    void StartApplication() override;

    // Called automatically by Application at the configured stop time.
    void StopApplication() override;

    /// Schedule a new transmission.
    void ScheduleTx();
    /// Send a packet.
    void SendPacket();

    Ptr<Socket> m_socket;   //!< Socket used to transmit every packet.
    Address m_peer;         //!< Remote IP address and transport-layer port.
    uint32_t m_packetSize;  //!< Application payload size in bytes.
    uint32_t m_nPackets;    //!< Maximum number of packets to transmit.
    DataRate m_dataRate;    //!< Rate used to calculate time between packets.
    EventId m_sendEvent;    //!< Handle for the next scheduled transmission.
    bool m_running;         //!< Whether the application may schedule more work.
    uint32_t m_packetsSent; //!< Number of packets transmitted so far.
};

} // namespace ns3: finish the ns-3 declarations.

#endif /* TUTORIAL_APP_H: finish the multiple-inclusion guard. */
