/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef ACTIVE_TIME_OBSERVE_MAC_FRAME_H
#define ACTIVE_TIME_OBSERVE_MAC_FRAME_H

#include "active-time-estimator.h"
#include "active-time-mac-transaction-tracker.h"

#include "ns3/wifi-module.h"

#include <map>

namespace ns3
{

void ObserveMacFrameRx(CatraActiveTimeEstimator* estimator,
                       CatraMacTransactionTracker* tracker,
                       const std::map<Mac48Address, Ptr<WifiNetDevice>>* devicesByAddress,
                       Ptr<WifiNetDevice> localReceiver,
                       Ptr<const Packet> packet,
                       uint16_t channelFreqMhz,
                       WifiTxVector txVector,
                       MpduInfo mpduInfo,
                       SignalNoiseDbm signalNoise,
                       uint16_t staId);

void ObserveMacFrameTx(CatraActiveTimeEstimator* estimator,
                       CatraMacTransactionTracker* tracker,
                       Ptr<const Packet> packet,
                       uint16_t channelFreqMhz,
                       WifiTxVector txVector,
                       MpduInfo mpduInfo,
                       uint16_t staId);

} // namespace ns3

#endif // ACTIVE_TIME_OBSERVE_MAC_FRAME_H
