/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef CATRA_TCP_TAHOE_H
#define CATRA_TCP_TAHOE_H

#include "ns3/tcp-congestion-ops.h"
#include "ns3/tcp-recovery-ops.h"

namespace ns3
{

/**
 * Tahoe congestion avoidance uses the same slow-start and additive-increase
 * rules as Reno. Its distinguishing behavior is supplied by
 * CatraTcpTahoeRecovery below.
 */
class CatraTcpTahoe : public TcpNewReno
{
  public:
    static TypeId GetTypeId();

    CatraTcpTahoe();
    CatraTcpTahoe(const CatraTcpTahoe& other);
    ~CatraTcpTahoe() override;

    std::string GetName() const override;
    Ptr<TcpCongestionOps> Fork() override;
};

/** Tahoe loss response: return to one MSS instead of Reno fast recovery. */
class CatraTcpTahoeRecovery : public TcpRecoveryOps
{
  public:
    static TypeId GetTypeId();

    CatraTcpTahoeRecovery();
    CatraTcpTahoeRecovery(const CatraTcpTahoeRecovery& other);
    ~CatraTcpTahoeRecovery() override;

    std::string GetName() const override;
    void EnterRecovery(Ptr<TcpSocketState> tcb,
                       uint32_t dupAckCount,
                       uint32_t unAckDataCount,
                       uint32_t deliveredBytes) override;
    void DoRecovery(Ptr<TcpSocketState> tcb,
                    uint32_t deliveredBytes,
                    bool isDupAck) override;
    void ExitRecovery(Ptr<TcpSocketState> tcb) override;
    Ptr<TcpRecoveryOps> Fork() override;
};

} // namespace ns3

#endif // CATRA_TCP_TAHOE_H
