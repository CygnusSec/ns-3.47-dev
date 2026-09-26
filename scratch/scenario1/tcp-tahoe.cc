/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "tcp-tahoe.h"

#include "ns3/log.h"
#include "ns3/tcp-socket-state.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("Scenario1TcpTahoe");
NS_OBJECT_ENSURE_REGISTERED(Scenario1TcpTahoe);
NS_OBJECT_ENSURE_REGISTERED(Scenario1TcpTahoeRecovery);

TypeId
Scenario1TcpTahoe::GetTypeId()
{
    static TypeId tid = TypeId("ns3::Scenario1TcpTahoe")
                            .SetParent<TcpNewReno>()
                            .SetGroupName("Internet")
                            .AddConstructor<Scenario1TcpTahoe>();
    return tid;
}

Scenario1TcpTahoe::Scenario1TcpTahoe() = default;

Scenario1TcpTahoe::Scenario1TcpTahoe(const Scenario1TcpTahoe& other)
    : TcpNewReno(other)
{
}

Scenario1TcpTahoe::~Scenario1TcpTahoe() = default;

std::string
Scenario1TcpTahoe::GetName() const
{
    return "Scenario1TcpTahoe";
}

Ptr<TcpCongestionOps>
Scenario1TcpTahoe::Fork()
{
    return CopyObject<Scenario1TcpTahoe>(this);
}

TypeId
Scenario1TcpTahoeRecovery::GetTypeId()
{
    static TypeId tid = TypeId("ns3::Scenario1TcpTahoeRecovery")
                            .SetParent<TcpRecoveryOps>()
                            .SetGroupName("Internet")
                            .AddConstructor<Scenario1TcpTahoeRecovery>();
    return tid;
}

Scenario1TcpTahoeRecovery::Scenario1TcpTahoeRecovery() = default;

Scenario1TcpTahoeRecovery::Scenario1TcpTahoeRecovery(const Scenario1TcpTahoeRecovery& other)
    : TcpRecoveryOps(other)
{
}

Scenario1TcpTahoeRecovery::~Scenario1TcpTahoeRecovery() = default;

std::string
Scenario1TcpTahoeRecovery::GetName() const
{
    return "Scenario1TcpTahoeRecovery";
}

void
Scenario1TcpTahoeRecovery::EnterRecovery(Ptr<TcpSocketState> tcb,
                                         uint32_t,
                                         uint32_t,
                                         uint32_t)
{
    // TcpSocketBase has already computed ssthresh from the flight size. Tahoe
    // retransmits the missing segment and restarts from one MSS.
    tcb->m_cWnd = tcb->m_segmentSize;
    tcb->m_cWndInfl = tcb->m_cWnd;
    NS_LOG_INFO("Tahoe loss response: cwnd reset to one MSS=" << tcb->m_segmentSize
                                                               << " ssthresh="
                                                               << tcb->m_ssThresh);
}

void
Scenario1TcpTahoeRecovery::DoRecovery(Ptr<TcpSocketState> tcb, uint32_t, bool)
{
    // Tahoe does not inflate its window for duplicate ACKs.
    tcb->m_cWnd = tcb->m_segmentSize;
    tcb->m_cWndInfl = tcb->m_cWnd;
}

void
Scenario1TcpTahoeRecovery::ExitRecovery(Ptr<TcpSocketState> tcb)
{
    // TcpSocketBase temporarily restores ssthresh before this callback. Put
    // the Tahoe sender back at one MSS so subsequent ACKs re-enter slow start.
    tcb->m_cWnd = tcb->m_segmentSize;
    tcb->m_cWndInfl = tcb->m_cWnd;
}

Ptr<TcpRecoveryOps>
Scenario1TcpTahoeRecovery::Fork()
{
    return CopyObject<Scenario1TcpTahoeRecovery>(this);
}

} // namespace ns3
