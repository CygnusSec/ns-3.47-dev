/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CATRA_MAC_CONTROLLER_H
#define CATRA_MAC_CONTROLLER_H

#include <cstdint>

namespace ns3
{

enum class CatraMacAction
{
    NO_DATA_SEND_FLOW,
    DECREASE_CW,
    KEEP_CW,
    INCREASE_CW
};

struct CatraMacDecision
{
    CatraMacAction action{CatraMacAction::NO_DATA_SEND_FLOW};
    double ratio{};
    double rawWindowSlots{};
    uint32_t windowSlots{};
    uint32_t ns3Cw{};
};

/** Calculate paper Eq. (5) without mutating a live Txop. */
CatraMacDecision CalculateCatraMacDecision(uint32_t currentNs3Cw,
                                           uint32_t maximumNs3Cw,
                                           double fairBandwidthRatio,
                                           double realBandwidthRatio);

const char* ToString(CatraMacAction action);

} // namespace ns3

#endif // CATRA_MAC_CONTROLLER_H
