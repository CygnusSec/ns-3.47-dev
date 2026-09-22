/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "catra-tcp-controller.h"

#include "ns3/core-module.h"

#include <cmath>
#include <iostream>
#include <string>

using namespace ns3;

namespace
{

bool
Check(const std::string& name, bool condition)
{
    std::cout << name << '=' << (condition ? "PASS" : "FAIL") << '\n';
    return condition;
}

CatraTcpInputs
MakeInputs(double ratio, uint32_t cwndSegments = 8)
{
    constexpr uint32_t MSS = 1024;
    CatraTcpInputs input;
    input.fairBandwidthRatio = 0.25;
    input.realBandwidthRatio = ratio * input.fairBandwidthRatio;
    input.activeTime = MilliSeconds(400);
    input.packetCount = 40;
    input.nTotal = 4;
    input.cwndBytes = cwndSegments * MSS;
    input.segmentSizeBytes = MSS;
    input.highestAckBytes = 120 * MSS;
    input.currentSequenceBytes = 122 * MSS;
    return input;
}

} // namespace

int
main(int argc, char* argv[])
{
    bool strict{true};
    CommandLine cmd(__FILE__);
    cmd.AddValue("strict", "Return failure unless every Algorithm 2 check passes", strict);
    cmd.Parse(argc, argv);

    bool passed = true;
    const auto high = CalculateCatraTcpDecision(MakeInputs(1.2));
    passed &= Check("high_action", high.action == CatraTcpAction::DECREASE_AND_DELAY);
    passed &= Check("high_cwnd_minus_one_mss", high.newCwndBytes == 7 * 1024);
    passed &= Check("high_delay_positive", high.delay.IsStrictlyPositive());
    passed &= Check("ttr_formula", high.averageTxTime == MilliSeconds(10));
    passed &= Check("win_formula", high.winBytes == 6 * 1024 && high.winPackets == 6.0);
    passed &= Check("tf_formula", high.fairTransmissionTime == MilliSeconds(240));
    passed &= Check("delta_formula", std::abs(high.delay.GetSeconds() - 0.288) < 1e-12);

    const auto low = CalculateCatraTcpDecision(MakeInputs(0.5));
    passed &= Check("low_action", low.action == CatraTcpAction::INCREASE);
    passed &= Check("low_cwnd_plus_one_mss", low.newCwndBytes == 9 * 1024);
    passed &= Check("low_delay_zero", low.delay.IsZero());

    const auto middleLow = CalculateCatraTcpDecision(MakeInputs(0.7));
    const auto middleHigh = CalculateCatraTcpDecision(MakeInputs(1.05));
    passed &= Check("low_boundary_original", middleLow.action == CatraTcpAction::ORIGINAL_TCP);
    passed &= Check("high_boundary_original", middleHigh.action == CatraTcpAction::ORIGINAL_TCP);

    const auto minimum = CalculateCatraTcpDecision(MakeInputs(1.2, 1));
    passed &= Check("one_mss_floor", minimum.newCwndBytes == 1024);
    auto invalidInput = MakeInputs(1.2);
    invalidInput.packetCount = 0;
    const auto invalid = CalculateCatraTcpDecision(invalidInput);
    passed &= Check("zero_packet_no_decision", invalid.action == CatraTcpAction::NO_DECISION);

    std::cout << "algorithm2_overall=" << (passed ? "PASS" : "FAIL") << '\n';
    return strict && !passed ? 1 : 0;
}
