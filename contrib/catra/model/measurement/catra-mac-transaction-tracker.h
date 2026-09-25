/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef CATRA_MAC_TRANSACTION_TRACKER_H
#define CATRA_MAC_TRANSACTION_TRACKER_H

#include "catra-active-time-estimator.h"

#include "ns3/mac48-address.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

namespace ns3
{

/** TCP-DATA information retained until the local station transmits its MAC ACK. */
struct PendingTcpData
{
    Mac48Address destId;
    Mac48Address localId;
    CatraTransactionTiming timing;
};

/** Correlates a received TCP-DATA MPDU with the normal MAC ACK sent to its sender. */
class CatraMacTransactionTracker
{
  public:
    void Store(Mac48Address sender, const PendingTcpData& pending);
    std::optional<PendingTcpData> Take(Mac48Address sender);

    uint64_t GetTcpDataFramesObserved() const;
    uint64_t GetMacAcksMatched() const;
    std::size_t GetPendingCount() const;

  private:
    std::map<Mac48Address, PendingTcpData> m_pendingTcpData;
    uint64_t m_tcpDataFramesObserved{0};
    uint64_t m_macAcksMatched{0};
};

} // namespace ns3

#endif // CATRA_MAC_TRANSACTION_TRACKER_H
