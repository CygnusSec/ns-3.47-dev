/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/catra-tcp-controller.h"

#include "ns3/core-module.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
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
    input.estimationPeriod = Seconds(2);
    input.nTotal = 4;
    // FBRf=1/4, so select TActiveFlow to produce the requested RBRf/FBRf ratio.
    input.activeTime = Seconds(ratio * input.estimationPeriod.GetSeconds() / input.nTotal);
    input.packetCount = 40;
    input.cwndBytes = cwndSegments * MSS;
    input.segmentSizeBytes = MSS;
    input.bytesInFlight = std::min<uint32_t>(2, cwndSegments) * MSS;
    return input;
}

void
PrintDecision(const std::string& testCase,
              const CatraTcpInputs& input,
              const CatraTcpDecision& decision,
              double highThreshold,
              double lowThreshold)
{
    std::cout << std::fixed << std::setprecision(6)
              << "\n[ALGORITHM2-BEGIN] case=" << testCase << '\n'
              << "  [INPUT] TActiveFlow_s=" << input.activeTime.GetSeconds()
              << " EP_s=" << input.estimationPeriod.GetSeconds()
              << " Nf=" << input.packetCount
              << " ntotal=" << input.nTotal << '\n'
              << "  [TCP-STATE] cwnd_bytes=" << input.cwndBytes
              << " mss_bytes=" << input.segmentSizeBytes
              << " bytes_in_flight=" << input.bytesInFlight << '\n'
              << "  [THRESHOLDS] Lowth=" << lowThreshold
              << " Highth=" << highThreshold
              << " comparisons=ratio<Lowth|ratio>Highth\n";

    if (decision.action == CatraTcpAction::NO_DECISION)
    {
        std::cout << "  [VALIDATION] input=INVALID_OR_INACTIVE"
                  << " reason=invalid_or_inactive_algorithm_input\n"
                  << "  [DECISION] action=" << ToString(decision.action)
                  << " cwnd_unchanged_bytes=" << decision.newCwndBytes
                  << " deltaF_s=" << decision.delay.GetSeconds() << '\n'
                  << "[ALGORITHM2-END] case=" << testCase << "\n";
        return;
    }

    std::cout << "  [STEP-1-FBR] FBRf=1/ntotal=1/" << input.nTotal << '='
              << decision.fairBandwidthRatio << '\n'
              << "  [STEP-2-RBR] RBRf=TActiveFlow/EP=" << input.activeTime.GetSeconds() << '/'
              << input.estimationPeriod.GetSeconds() << '=' << decision.realBandwidthRatio << '\n'
              << "  [STEP-3-RATIO] ratio=RBRf/FBRf=" << decision.realBandwidthRatio << '/'
              << decision.fairBandwidthRatio << '=' << decision.ratio << '\n'
              << "  [STEP-4-TTR] Ttr_f=TActiveFlow/Nf=" << input.activeTime.GetSeconds()
              << '/' << input.packetCount << '=' << decision.averageTxTime.GetSeconds()
              << "s\n"
              << "  [STEP-5-WIN] win_bytes=cwnd-bytesInFlight="
              << input.cwndBytes << '-' << input.bytesInFlight << '=' << decision.winBytes
              << " win_packets=win_bytes/MSS=" << decision.winPackets << '\n'
              << "  [STEP-6-TF] Tf=ntotal*win*Ttr_f=" << input.nTotal << '*'
              << decision.winPackets << '*' << decision.averageTxTime.GetSeconds() << '='
              << decision.fairTransmissionTime.GetSeconds() << "s\n"
              << "  [STEP-7-BRANCH] action=" << ToString(decision.action)
              << " ratio=" << decision.ratio << '\n'
              << "  [STEP-8-CWND] old_cwnd_bytes=" << input.cwndBytes
              << " new_cwnd_bytes=" << decision.newCwndBytes
              << " delta_bytes="
              << static_cast<int64_t>(decision.newCwndBytes) - input.cwndBytes << '\n'
              << "  [STEP-9-DELAY] deltaF_s=" << decision.delay.GetSeconds();
    if (decision.action == CatraTcpAction::DECREASE_AND_DELAY)
    {
        std::cout << " formula=ratio*Tf=" << decision.ratio << '*'
                  << decision.fairTransmissionTime.GetSeconds();
    }
    else
    {
        std::cout << " formula=0";
    }
    std::cout << '\n' << "[ALGORITHM2-END] case=" << testCase << "\n";
}

} // namespace

int
main(int argc, char* argv[])
{
    bool strict{true};
    bool verbose{true};
    constexpr double HIGH_THRESHOLD = 1.05;
    constexpr double LOW_THRESHOLD = 0.7;
    CommandLine cmd(__FILE__);
    cmd.AddValue("strict", "Return failure unless every Algorithm 2 check passes", strict);
    cmd.AddValue("verbose", "Print every Algorithm 2 input and intermediate calculation", verbose);
    cmd.Parse(argc, argv);

    bool passed = true;
    const auto highInput = MakeInputs(1.2);
    const auto high = CalculateCatraTcpDecision(highInput, HIGH_THRESHOLD, LOW_THRESHOLD);
    if (verbose)
    {
        PrintDecision("above-high-threshold", highInput, high, HIGH_THRESHOLD, LOW_THRESHOLD);
    }
    passed &= Check("high_action", high.action == CatraTcpAction::DECREASE_AND_DELAY);
    passed &= Check("fbr_formula", high.fairBandwidthRatio == 0.25);
    passed &= Check("rbr_formula", std::abs(high.realBandwidthRatio - 0.3) < 1e-12);
    passed &= Check("high_cwnd_minus_one_mss", high.newCwndBytes == 7 * 1024);
    passed &= Check("high_delay_positive", high.delay.IsStrictlyPositive());
    passed &= Check("ttr_formula", high.averageTxTime == MilliSeconds(15));
    passed &= Check("win_formula", high.winBytes == 6 * 1024 && high.winPackets == 6.0);
    passed &= Check("tf_formula", high.fairTransmissionTime == MilliSeconds(360));
    passed &= Check("delta_formula", std::abs(high.delay.GetSeconds() - 0.432) < 1e-12);

    const auto lowInput = MakeInputs(0.5);
    const auto low = CalculateCatraTcpDecision(lowInput, HIGH_THRESHOLD, LOW_THRESHOLD);
    if (verbose)
    {
        PrintDecision("below-low-threshold", lowInput, low, HIGH_THRESHOLD, LOW_THRESHOLD);
    }
    passed &= Check("low_action", low.action == CatraTcpAction::INCREASE);
    passed &= Check("low_cwnd_plus_one_mss", low.newCwndBytes == 9 * 1024);
    passed &= Check("low_delay_zero", low.delay.IsZero());

    const auto middleLowInput = MakeInputs(LOW_THRESHOLD);
    const auto middleHighInput = MakeInputs(HIGH_THRESHOLD);
    const auto middleLow = CalculateCatraTcpDecision(middleLowInput, HIGH_THRESHOLD, LOW_THRESHOLD);
    const auto middleHigh =
        CalculateCatraTcpDecision(middleHighInput, HIGH_THRESHOLD, LOW_THRESHOLD);
    if (verbose)
    {
        PrintDecision("equal-low-threshold",
                      middleLowInput,
                      middleLow,
                      HIGH_THRESHOLD,
                      LOW_THRESHOLD);
        PrintDecision("equal-high-threshold",
                      middleHighInput,
                      middleHigh,
                      HIGH_THRESHOLD,
                      LOW_THRESHOLD);
    }
    passed &= Check("low_boundary_original", middleLow.action == CatraTcpAction::ORIGINAL_TCP);
    passed &= Check("high_boundary_original", middleHigh.action == CatraTcpAction::ORIGINAL_TCP);

    const auto minimumInput = MakeInputs(1.2, 1);
    const auto minimum =
        CalculateCatraTcpDecision(minimumInput, HIGH_THRESHOLD, LOW_THRESHOLD);
    if (verbose)
    {
        PrintDecision("one-mss-floor", minimumInput, minimum, HIGH_THRESHOLD, LOW_THRESHOLD);
    }
    passed &= Check("one_mss_floor", minimum.newCwndBytes == 1024);
    auto invalidInput = MakeInputs(1.2);
    invalidInput.packetCount = 0;
    const auto invalid = CalculateCatraTcpDecision(invalidInput, HIGH_THRESHOLD, LOW_THRESHOLD);
    if (verbose)
    {
        PrintDecision("zero-packet", invalidInput, invalid, HIGH_THRESHOLD, LOW_THRESHOLD);
    }
    passed &= Check("zero_packet_no_decision", invalid.action == CatraTcpAction::NO_DECISION);

    auto zeroEpInput = MakeInputs(1.2);
    zeroEpInput.estimationPeriod = Time(0);
    passed &= Check("zero_ep_no_decision",
                    CalculateCatraTcpDecision(zeroEpInput).action ==
                        CatraTcpAction::NO_DECISION);
    auto excessiveFlightInput = MakeInputs(1.2);
    excessiveFlightInput.bytesInFlight = excessiveFlightInput.cwndBytes + 1;
    passed &= Check("flight_above_cwnd_no_decision",
                    CalculateCatraTcpDecision(excessiveFlightInput).action ==
                        CatraTcpAction::NO_DECISION);
    passed &= Check("invalid_low_threshold_no_decision",
                    CalculateCatraTcpDecision(MakeInputs(1.0), 1.05, 1.01).action ==
                        CatraTcpAction::NO_DECISION);
    passed &= Check("invalid_high_threshold_no_decision",
                    CalculateCatraTcpDecision(MakeInputs(1.0), 0.99, 0.7).action ==
                        CatraTcpAction::NO_DECISION);

    // Paper Eq. (12): nTotal=3, FBRf=1/3 and RBRf=1/2 produce ratio=3/2,
    // Tf=3*win*Ttr and deltaF=(3/2)*Tf=(9/2)*win*Ttr.
    CatraTcpInputs paperExample;
    paperExample.activeTime = Seconds(1.0);
    paperExample.estimationPeriod = Seconds(2.0);
    paperExample.packetCount = 100;
    paperExample.nTotal = 3;
    paperExample.cwndBytes = 10 * 1024;
    paperExample.segmentSizeBytes = 1024;
    paperExample.bytesInFlight = 0;
    const auto paperDecision =
        CalculateCatraTcpDecision(paperExample, HIGH_THRESHOLD, LOW_THRESHOLD);
    if (verbose)
    {
        PrintDecision("paper-equation-12",
                      paperExample,
                      paperDecision,
                      HIGH_THRESHOLD,
                      LOW_THRESHOLD);
    }
    passed &= Check("paper_example_fbr", std::abs(paperDecision.fairBandwidthRatio - 1.0 / 3.0) < 1e-12);
    passed &= Check("paper_example_rbr", std::abs(paperDecision.realBandwidthRatio - 0.5) < 1e-12);
    passed &= Check("paper_example_ratio", std::abs(paperDecision.ratio - 1.5) < 1e-12);
    passed &= Check("paper_example_tf", paperDecision.fairTransmissionTime == MilliSeconds(300));
    passed &= Check("paper_example_delta", paperDecision.delay == MilliSeconds(450));

    std::cout << "algorithm2_overall=" << (passed ? "PASS" : "FAIL") << '\n';
    return strict && !passed ? 1 : 0;
}
