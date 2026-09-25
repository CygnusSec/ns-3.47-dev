/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "tcp-tahoe.h"

#include "ns3/log.h"
#include "ns3/tcp-socket-state.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("CatraTcpTahoe");
NS_OBJECT_ENSURE_REGISTERED(CatraTcpTahoe);
NS_OBJECT_ENSURE_REGISTERED(CatraTcpTahoeRecovery);

TypeId
CatraTcpTahoe::GetTypeId()
{
    static TypeId tid = TypeId("ns3::CatraTcpTahoe")
                            .SetParent<TcpNewReno>()
                            .SetGroupName("Internet")
                            .AddConstructor<CatraTcpTahoe>();
    return tid;
}

CatraTcpTahoe::CatraTcpTahoe() = default;

CatraTcpTahoe::CatraTcpTahoe(const CatraTcpTahoe& other)
    : TcpNewReno(other)
{
}

CatraTcpTahoe::~CatraTcpTahoe() = default;

std::string
CatraTcpTahoe::GetName() const
{
    return "CatraTcpTahoe";
}

Ptr<TcpCongestionOps>
CatraTcpTahoe::Fork()
{
    return CopyObject<CatraTcpTahoe>(this);
}

TypeId
CatraTcpTahoeRecovery::GetTypeId()
{
    static TypeId tid = TypeId("ns3::CatraTcpTahoeRecovery")
                            .SetParent<TcpRecoveryOps>()
                            .SetGroupName("Internet")
                            .AddConstructor<CatraTcpTahoeRecovery>();
    return tid;
}

CatraTcpTahoeRecovery::CatraTcpTahoeRecovery() = default;

CatraTcpTahoeRecovery::CatraTcpTahoeRecovery(const CatraTcpTahoeRecovery& other)
    : TcpRecoveryOps(other)
{
}

CatraTcpTahoeRecovery::~CatraTcpTahoeRecovery() = default;

std::string
CatraTcpTahoeRecovery::GetName() const
{
    return "CatraTcpTahoeRecovery";
}

void
CatraTcpTahoeRecovery::EnterRecovery(Ptr<TcpSocketState> tcb,
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
CatraTcpTahoeRecovery::DoRecovery(Ptr<TcpSocketState> tcb, uint32_t, bool)
{
    // Tahoe does not inflate its window for duplicate ACKs.
    tcb->m_cWnd = tcb->m_segmentSize;
    tcb->m_cWndInfl = tcb->m_cWnd;
}

void
CatraTcpTahoeRecovery::ExitRecovery(Ptr<TcpSocketState> tcb)
{
    // TcpSocketBase temporarily restores ssthresh before this callback. Put
    // the Tahoe sender back at one MSS so subsequent ACKs re-enter slow start.
    tcb->m_cWnd = tcb->m_segmentSize;
    tcb->m_cWndInfl = tcb->m_cWnd;
}

Ptr<TcpRecoveryOps>
CatraTcpTahoeRecovery::Fork()
{
    return CopyObject<CatraTcpTahoeRecovery>(this);
}

} // namespace ns3
