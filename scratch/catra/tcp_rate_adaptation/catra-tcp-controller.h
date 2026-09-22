/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef CATRA_TCP_CONTROLLER_H
#define CATRA_TCP_CONTROLLER_H

#include "ns3/nstime.h"

#include <cstdint>

namespace ns3
{

enum class CatraTcpAction
{
    NO_DECISION,
    DECREASE_AND_DELAY,
    INCREASE,
    ORIGINAL_TCP
};

struct CatraTcpInputs
{
    double fairBandwidthRatio{}; //!< FBRf = 1 / ntotal.
    double realBandwidthRatio{}; //!< RBRf = TActiveFlow / EP.
    Time activeTime{};           //!< TActiveFlow accumulated during the EP.
    uint64_t packetCount{};      //!< Nf packets transmitted during the EP.
    uint32_t nTotal{};           //!< Flows sharing the examined channel.
    uint32_t cwndBytes{};
    uint32_t segmentSizeBytes{};
    uint32_t highestAckBytes{};
    uint32_t currentSequenceBytes{};
};

struct CatraTcpDecision
{
    CatraTcpAction action{CatraTcpAction::NO_DECISION};
    double ratio{};              //!< RBRf / FBRf.
    Time averageTxTime{};        //!< Ttr_f = TActiveFlow / Nf.
    uint64_t winBytes{};         //!< cwnd + highestAck - currentSequence.
    double winPackets{};         //!< win converted from bytes to MSS units.
    Time fairTransmissionTime{}; //!< Tf = ntotal * win * Ttr_f.
    Time delay{};                //!< deltaF; zero unless bandwidth is excessive.
    uint32_t newCwndBytes{};
};

/** Pure, side-effect-free implementation of paper Algorithm 2. */
CatraTcpDecision CalculateCatraTcpDecision(const CatraTcpInputs& inputs,
                                           double highThreshold = 1.05,
                                           double lowThreshold = 0.7);

const char* ToString(CatraTcpAction action);

} // namespace ns3

#endif // CATRA_TCP_CONTROLLER_H
