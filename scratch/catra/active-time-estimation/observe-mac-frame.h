/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef OBSERVE_MAC_FRAME_H
#define OBSERVE_MAC_FRAME_H

#include "catra-active-time-estimator.h"
#include "catra-mac-transaction-tracker.h"

#include "ns3/wifi-module.h"

namespace ns3
{

void ObserveMacFrameRx(CatraActiveTimeEstimator* estimator,
                       CatraMacTransactionTracker* tracker,
                       Ptr<WifiNetDevice> peerSender,
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

#endif // OBSERVE_MAC_FRAME_H
