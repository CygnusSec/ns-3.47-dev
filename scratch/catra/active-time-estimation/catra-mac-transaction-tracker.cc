/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "catra-mac-transaction-tracker.h"

namespace ns3
{

void
CatraMacTransactionTracker::Store(Mac48Address sender, const PendingTcpData& pending)
{
    m_pendingTcpData.insert_or_assign(sender, pending);
    ++m_tcpDataFramesObserved;
}

std::optional<PendingTcpData>
CatraMacTransactionTracker::Take(Mac48Address sender)
{
    const auto it = m_pendingTcpData.find(sender);
    if (it == m_pendingTcpData.end())
    {
        return std::nullopt;
    }
    PendingTcpData pending = it->second;
    m_pendingTcpData.erase(it);
    ++m_macAcksMatched;
    return pending;
}

uint64_t
CatraMacTransactionTracker::GetTcpDataFramesObserved() const
{
    return m_tcpDataFramesObserved;
}

uint64_t
CatraMacTransactionTracker::GetMacAcksMatched() const
{
    return m_macAcksMatched;
}

std::size_t
CatraMacTransactionTracker::GetPendingCount() const
{
    return m_pendingTcpData.size();
}

} // namespace ns3
