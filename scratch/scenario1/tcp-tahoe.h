/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SCENARIO1_TCP_TAHOE_H
#define SCENARIO1_TCP_TAHOE_H

#include "ns3/tcp-congestion-ops.h"
#include "ns3/tcp-recovery-ops.h"

namespace ns3
{

/**
 * Tahoe congestion avoidance uses the same slow-start and additive-increase
 * rules as Reno. Its distinguishing behavior is supplied by
 * Scenario1TcpTahoeRecovery below.
 */
class Scenario1TcpTahoe : public TcpNewReno
{
  public:
    static TypeId GetTypeId();

    Scenario1TcpTahoe();
    Scenario1TcpTahoe(const Scenario1TcpTahoe& other);
    ~Scenario1TcpTahoe() override;

    std::string GetName() const override;
    Ptr<TcpCongestionOps> Fork() override;
};

/** Tahoe loss response: return to one MSS instead of Reno fast recovery. */
class Scenario1TcpTahoeRecovery : public TcpRecoveryOps
{
  public:
    static TypeId GetTypeId();

    Scenario1TcpTahoeRecovery();
    Scenario1TcpTahoeRecovery(const Scenario1TcpTahoeRecovery& other);
    ~Scenario1TcpTahoeRecovery() override;

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

#endif // SCENARIO1_TCP_TAHOE_H
