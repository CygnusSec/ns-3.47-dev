/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "catra-active-time-estimator.h"

#include "ns3/assert.h"
#include "ns3/simulator.h"

namespace ns3
{
    // Invalid weights would silently create a nonsensical EWMA, while a zero
    // EP would also cause division by zero in GetRealBandwidthRatio(). Fail at
    // construction time so every later sample has well-defined semantics.

CatraActiveTimeEstimator::CatraActiveTimeEstimator(uint32_t stationIndex,
                                                   Time estimationPeriod,
                                                   double historyWeight,
                                                   ReportCallback reportCallback)
    : m_stationIndex(stationIndex),
      m_estimationPeriod(estimationPeriod),
      m_historyWeight(historyWeight),
      m_reportCallback(std::move(reportCallback))
{
    NS_ABORT_MSG_IF(estimationPeriod <= Time(0), "CATRA estimation period must be positive");
    NS_ABORT_MSG_IF(historyWeight < 0.0 || historyWeight > 1.0,
                    "CATRA history weight must be in [0, 1]");
}

void
CatraActiveTimeEstimator::Start(Time startTime)
{
    NS_ABORT_MSG_IF(m_started, "CATRA active-time estimator can only be started once");
    // All period boundaries are derived from the caller-supplied start time,
    // not from the time at which this method happens to execute. This keeps
    // samples from different stations aligned on identical EP boundaries.
    m_started = true;
    m_periodStart = startTime;
    m_periodEnd = startTime + m_estimationPeriod;
    m_stopTime = Time::Max();
    // ClosePeriod schedules all later reports, so there is only one periodic
    // event chain per station.
    Simulator::Schedule(m_periodEnd - Simulator::Now(),
                        &CatraActiveTimeEstimator::ClosePeriod,
                        this);
}

void
CatraActiveTimeEstimator::Stop(Time stopTime)
{
    // The already scheduled ClosePeriod event is left in the event queue. It
    // observes this boundary and exits cleanly, avoiding EventId ownership and
    // cancellation complexity in this small measurement component.
    m_stopTime = stopTime;
}

void
CatraActiveTimeEstimator::NotifyTcpTransaction(CatraTcpPacketType packetType,
                                               const CatraTransactionTiming& timing)
{
    if (!m_started)
    {
        return;
    }
    if (packetType == CatraTcpPacketType::DATA)
    {
        ++m_tcpDataPackets;
        ++m_totalTcpDataPackets;
    }
    else
    {
        ++m_tcpAckPackets;
        ++m_totalTcpAckPackets;
    }

    const Time expectedBackoff =
        Seconds(0.5 * timing.contentionWindow * timing.slotTime.GetSeconds());
    const Time interframeTime = 3 * timing.sifs + timing.difs;
    const Time transactionTime = expectedBackoff + timing.rtsTime + timing.ctsTime +
                                 timing.tcpFrameTime + timing.macAckTime + interframeTime;
    m_rawActiveTime += transactionTime;
    m_contentionWindowSum += timing.contentionWindow;
    m_slotTime = timing.slotTime;
    m_expectedBackoffTime += expectedBackoff;
    m_rtsTime += timing.rtsTime;
    m_ctsTime += timing.ctsTime;
    m_tcpFrameTime += timing.tcpFrameTime;
    m_macAckTime += timing.macAckTime;
    m_interframeTime += interframeTime;
}

void
CatraActiveTimeEstimator::ClosePeriod()
{
    if (m_periodEnd > m_stopTime)
    {
        // Do not normalize a partial tail interval by a full EP. Such a sample
        // would make RBRs artificially small and could mislead later CW logic.
        return;
    }

    // Algorithm 1 applies an exponentially weighted moving average:
    //   TActive(k) = 0.8 * TActive(k-1) + 0.2 * t(k)
    // The initial TActive is zero by construction, matching the paper.
    const double currentWeight = 1.0 - m_historyWeight;
    const Time previousSmoothedActiveTime = m_smoothedActiveTime;
    m_smoothedActiveTime = Seconds(m_historyWeight * m_smoothedActiveTime.GetSeconds() +
                                   currentWeight * m_rawActiveTime.GetSeconds());

    // Snapshot every value before resetting the raw period counters. The
    // callback therefore sees one internally consistent completed period.
    CatraActiveTimeSample sample;
    sample.stationIndex = m_stationIndex;
    sample.periodStart = m_periodStart;
    sample.periodEnd = m_periodEnd;
    sample.rawActiveTime = m_rawActiveTime;
    sample.previousSmoothedActiveTime = previousSmoothedActiveTime;
    sample.smoothedActiveTime = m_smoothedActiveTime;
    sample.realBandwidthRatio = GetRealBandwidthRatio();
    sample.tcpDataPackets = m_tcpDataPackets;
    sample.tcpAckPackets = m_tcpAckPackets;
    const uint64_t packetCount = m_tcpDataPackets + m_tcpAckPackets;
    sample.averageContentionWindow =
        packetCount == 0 ? 0.0 : static_cast<double>(m_contentionWindowSum) / packetCount;
    sample.slotTime = m_slotTime;
    sample.expectedBackoffTime = m_expectedBackoffTime;
    sample.rtsTime = m_rtsTime;
    sample.ctsTime = m_ctsTime;
    sample.tcpFrameTime = m_tcpFrameTime;
    sample.macAckTime = m_macAckTime;
    sample.interframeTime = m_interframeTime;
    if (m_reportCallback)
    {
        m_reportCallback(sample);
    }

    // Advance to the adjacent non-overlapping EP. TActive is deliberately kept
    // as EWMA history; only current-period raw counters are cleared.
    m_periodStart = m_periodEnd;
    m_periodEnd += m_estimationPeriod;
    m_rawActiveTime = Time(0);
    m_tcpDataPackets = 0;
    m_tcpAckPackets = 0;
    m_contentionWindowSum = 0;
    m_slotTime = Time(0);
    m_expectedBackoffTime = Time(0);
    m_rtsTime = Time(0);
    m_ctsTime = Time(0);
    m_tcpFrameTime = Time(0);
    m_macAckTime = Time(0);
    m_interframeTime = Time(0);
    Simulator::Schedule(m_estimationPeriod, &CatraActiveTimeEstimator::ClosePeriod, this);
}

Time
CatraActiveTimeEstimator::GetSmoothedActiveTime() const
{
    return m_smoothedActiveTime;
}

double
CatraActiveTimeEstimator::GetRealBandwidthRatio() const
{
    // Both numerator and denominator are times, so RBRs is dimensionless. A
    // value of 0.25 means smoothed local TX airtime occupies 25% of one EP.
    return m_smoothedActiveTime.GetSeconds() / m_estimationPeriod.GetSeconds();
}

uint64_t
CatraActiveTimeEstimator::GetTotalTcpDataPackets() const
{
    return m_totalTcpDataPackets;
}

uint64_t
CatraActiveTimeEstimator::GetTotalTcpAckPackets() const
{
    return m_totalTcpAckPackets;
}

} // namespace ns3
