/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef CATRA_ACTIVE_TIME_ESTIMATOR_H
#define CATRA_ACTIVE_TIME_ESTIMATOR_H

#include "ns3/nstime.h"
#include "ns3/wifi-phy-state.h"

#include <cstdint>
#include <functional>
#include <string>

namespace ns3
{

/**
 * Immutable reporting data produced when one estimation period (EP) closes.
 *
 * Keeping the report separate from the estimator's mutable state prevents a
 * logger or a future MAC controller from modifying the counters that will be
 * used by the next period.
 */
struct CatraActiveTimeSample
{
    uint32_t stationIndex{};    //!< Scenario-local station index owning this measurement.
    Time periodStart{};         //!< Inclusive beginning of the completed EP.
    Time periodEnd{};           //!< Exclusive end of the completed EP.
    Time rawTxTime{};           //!< Sum of local PHY TX durations observed during this EP.
    Time previousSmoothedActiveTime{}; //!< TActive carried into this EP.
    Time smoothedActiveTime{};  //!< Algorithm 1 TActive after applying EWMA smoothing.
    double realBandwidthRatio{}; //!< RBRs = smoothedActiveTime / EP.
    uint64_t txStateEvents{};   //!< Number of TX state transitions, useful for trace auditing.
};

/**
 * Read-only implementation of the smoothing part of CATRA Algorithm 1.
 *
 * The paper estimates the complete time occupied by TCP DATA and TCP ACK
 * transactions.  ns-3 exposes exact PHY state durations, so this first port
 * records local PHY TX time without changing the MAC.  Keeping this observable
 * separate makes the 0.8/0.2 estimator independently testable before packet
 * classification and contention-window control are enabled.
 */
class CatraActiveTimeEstimator
{
  public:
    /** Consumer invoked synchronously after a complete sample is constructed. */
    using ReportCallback = std::function<void(const CatraActiveTimeSample&)>;

    /**
     * Create one estimator for one station.
     *
     * @param stationIndex Scenario-local index printed in every report.
     * @param estimationPeriod Paper parameter EP (normally two seconds).
     * @param historyWeight Weight applied to the previous TActive (0.8 in the paper).
     * @param reportCallback Consumer for completed periods; it may be empty.
     */
    CatraActiveTimeEstimator(uint32_t stationIndex,
                             Time estimationPeriod,
                             double historyWeight,
                             ReportCallback reportCallback);

    /**
     * Consume a PHY state transition.
     *
     * Only TX contributes to this read-only port. RX, CCA_BUSY, IDLE, SLEEP,
     * and OFF transitions are deliberately ignored. The callback signature
     * matches WifiPhyStateHelper::StateTracedCallback exactly.
     */
    void NotifyPhyState(Time start, Time duration, WifiPhyState state);

    /**
     * Start periodic reporting. The first interval is
     * [startTime, startTime + EP). This method must be called exactly once.
     */
    void Start(Time startTime);

    /**
     * Define the last permitted period boundary. A partial period whose end is
     * later than stopTime is intentionally not reported as a full EP.
     */
    void Stop(Time stopTime);

    /** Return the last computed TActive value. */
    Time GetSmoothedActiveTime() const;

    /** Return the last computed RBRs = TActive / EP. */
    double GetRealBandwidthRatio() const;

  private:
    /** Finalize one EP, report it, reset raw counters, and schedule the next EP. */
    void ClosePeriod();

    /** Add only the part of a TX interval that overlaps the current EP. */
    void AddTxOverlap(Time start, Time end);

    uint32_t m_stationIndex;         //!< Owner of this per-station estimator.
    Time m_estimationPeriod;         //!< Fixed EP used for reports and RBRs normalization.
    double m_historyWeight;          //!< EWMA weight of the preceding TActive value.
    ReportCallback m_reportCallback; //!< Read-only destination for completed samples.
    Time m_periodStart;              //!< Beginning of the currently open EP.
    Time m_periodEnd;                //!< End boundary of the currently open EP.
    Time m_stopTime;                 //!< Last boundary that may produce a report.
    Time m_rawTxTime;                //!< Unsmooth TX duration accumulated in the current EP.
    Time m_smoothedActiveTime;       //!< TActive carried between consecutive periods.
    uint64_t m_txStateEvents{0};     //!< TX transitions accepted in the current EP.
    bool m_started{false};           //!< Guards against accidental duplicate schedules.
};

} // namespace ns3

#endif // CATRA_ACTIVE_TIME_ESTIMATOR_H
