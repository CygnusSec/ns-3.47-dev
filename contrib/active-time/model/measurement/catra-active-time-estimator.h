/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef CATRA_ACTIVE_TIME_ESTIMATOR_H
#define CATRA_ACTIVE_TIME_ESTIMATOR_H

#include "ns3/nstime.h"
#include <cstdint>
#include <functional>

namespace ns3
{

enum class CatraTcpPacketType
{
    DATA,
    ACK
};

/** Timing terms for one complete RTS/CTS/DATA-or-TCP-ACK/MAC-ACK transaction. */
struct CatraTransactionTiming
{
    uint32_t contentionWindow{};
    Time slotTime{};
    Time rtsTime{};
    Time ctsTime{};
    Time tcpFrameTime{};
    Time macAckTime{};
    Time sifs{};
    Time difs{};
};

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
    Time rawActiveTime{};       //!< Algorithm 1 accumulator t for the completed EP.
    Time previousSmoothedActiveTime{}; //!< TActive carried into this EP.
    Time smoothedActiveTime{};  //!< Algorithm 1 TActive after applying EWMA smoothing.
    double realBandwidthRatio{}; //!< RBRs = smoothedActiveTime / EP.
    uint64_t tcpDataPackets{};  //!< TCP-DATA transactions counted in this EP.
    uint64_t tcpAckPackets{};   //!< TCP-ACK transactions counted in this EP.
    double averageContentionWindow{}; //!< Mean sender CW read for accepted packets.
    Time slotTime{};             //!< Sender slot time (constant in this probe).
    Time expectedBackoffTime{}; //!< Sum of 0.5 * CW * slot-time terms.
    Time rtsTime{};             //!< Sum of modeled RTS durations.
    Time ctsTime{};             //!< Sum of modeled CTS durations.
    Time tcpFrameTime{};        //!< Sum of TCP-DATA or TCP-ACK frame durations.
    Time macAckTime{};          //!< Sum of modeled MAC ACK durations.
    Time interframeTime{};      //!< Sum of three SIFS plus one DIFS per transaction.
};

/**
 * Read-only implementation of CATRA Algorithm 1 active-time estimation.
 *
 * The probe supplies local-destination TCP-DATA transactions whose MAC ACK
 * transmissions were explicitly observed, and local-destination MAC DATA MPDUs
 * carrying pure TCP-ACK packets. Each contributes the paper's
 * complete modeled transaction time: expected backoff, RTS, CTS, three SIFS,
 * the TCP-bearing frame, a MAC ACK, and DIFS. CW is read only.
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

    /** Count one local-destination TCP transaction using Algorithm 1. */
    void NotifyTcpTransaction(CatraTcpPacketType packetType,
                              const CatraTransactionTiming& timing);

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

    uint64_t GetTotalTcpDataPackets() const;
    uint64_t GetTotalTcpAckPackets() const;

  private:
    /** Finalize one EP, report it, reset raw counters, and schedule the next EP. */
    void ClosePeriod();

    uint32_t m_stationIndex;         //!< Owner of this per-station estimator.
    Time m_estimationPeriod;         //!< Fixed EP used for reports and RBRs normalization.
    double m_historyWeight;          //!< EWMA weight of the preceding TActive value.
    ReportCallback m_reportCallback; //!< Read-only destination for completed samples.
    Time m_periodStart;              //!< Beginning of the currently open EP.
    Time m_periodEnd;                //!< End boundary of the currently open EP.
    Time m_stopTime;                 //!< Last boundary that may produce a report.
    Time m_rawActiveTime;            //!< Algorithm 1 accumulator t for the current EP.
    Time m_smoothedActiveTime;       //!< TActive carried between consecutive periods.
    uint64_t m_tcpDataPackets{0};
    uint64_t m_tcpAckPackets{0};
    uint64_t m_totalTcpDataPackets{0};
    uint64_t m_totalTcpAckPackets{0};
    uint64_t m_contentionWindowSum{0};
    Time m_slotTime;
    Time m_expectedBackoffTime;
    Time m_rtsTime;
    Time m_ctsTime;
    Time m_tcpFrameTime;
    Time m_macAckTime;
    Time m_interframeTime;
    bool m_started{false};           //!< Guards against accidental duplicate schedules.
};

} // namespace ns3

#endif // CATRA_ACTIVE_TIME_ESTIMATOR_H
