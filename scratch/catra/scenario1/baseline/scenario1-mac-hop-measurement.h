/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SCENARIO1_MAC_HOP_MEASUREMENT_H
#define SCENARIO1_MAC_HOP_MEASUREMENT_H

#include "ns3/net-device-container.h"
#include "ns3/packet.h"
#include "ns3/wifi-phy.h"
#include "ns3/wifi-tx-vector.h"

#include <cstdint>
#include <map>
#include <ostream>
#include <string>
#include <vector>

namespace ns3
{

/**
 * Measure every transmitted TCP-related MAC frame on each adjacent hop.
 *
 * MonitorSnifferTx fires for every PHY attempt, therefore retransmitted DATA,
 * RTS, CTS and normal MAC ACK frames are included. Results are emitted for
 * both directions and for their per-hop aggregate in fixed time intervals.
 */
class Scenario1MacHopMeasurement
{
  public:
    Scenario1MacHopMeasurement(uint32_t stationCount,
                               double startTimeS,
                               double stopTimeS,
                               double intervalS,
                               std::string csvPath,
                               std::string trafficProfile,
                               bool catraEnabled,
                               uint32_t seed,
                               uint64_t run,
                               std::string adjacentDistances);

    void RegisterDevices(const NetDeviceContainer& devices);

    void ObserveTx(uint32_t senderIndex,
                   Ptr<WifiPhy> phy,
                   Ptr<const Packet> packet,
                   uint16_t channelFreqMhz,
                   WifiTxVector txVector,
                   MpduInfo mpduInfo,
                   uint16_t staId);

    void WriteCsv() const;

  private:
    enum class TcpFlow : uint8_t
    {
        NONE,
        FLOW1,
        FLOW2,
        TCP_STRESS
    };

    enum class TcpPacketKind : uint8_t
    {
        NONE,
        DATA,
        PURE_ACK,
        OTHER
    };

    struct FlowCounters
    {
        uint64_t dataFrames{};
        uint64_t dataBytes{};
        uint64_t tcpAckFrames{};
        uint64_t tcpAckBytes{};
        uint64_t retryFrames{};
        uint64_t retryBytes{};
    };

    struct Counters
    {
        FlowCounters flow1;
        FlowCounters flow2;
        FlowCounters tcpStress;
        uint64_t otherTcpFrames{};
        uint64_t otherTcpBytes{};
        uint64_t macDataFrames{};
        uint64_t macDataBytes{};
        uint64_t rtsFrames{};
        uint64_t rtsBytes{};
        uint64_t ctsFrames{};
        uint64_t ctsBytes{};
        uint64_t macAckFrames{};
        uint64_t macAckBytes{};
        uint64_t totalFrames{};
        uint64_t totalBytes{};
        double txAirtimeS{};
    };

    struct ParsedTcp
    {
        TcpFlow flow{TcpFlow::NONE};
        TcpPacketKind kind{TcpPacketKind::NONE};
    };

    static ParsedTcp ParseTcp(Ptr<const Packet> packet);
    static void Add(Counters& destination, const Counters& source);
    static void WriteCounters(std::ostream& output,
                              const Counters& counters,
                              double intervalDurationS);
    Counters& GetCounters(uint32_t intervalIndex,
                          uint32_t hopIndex,
                          uint32_t directionIndex);

    uint32_t m_stationCount;
    double m_startTimeS;
    double m_stopTimeS;
    double m_intervalS;
    uint32_t m_intervalCount;
    std::string m_csvPath;
    std::string m_trafficProfile;
    bool m_catraEnabled;
    uint32_t m_seed;
    uint64_t m_run;
    std::string m_adjacentDistances;
    std::map<Mac48Address, uint32_t> m_nodeByAddress;
    std::vector<Counters> m_counters;
};

void ObserveScenario1MacHopTx(Scenario1MacHopMeasurement* measurement,
                              uint32_t senderIndex,
                              Ptr<WifiPhy> phy,
                              Ptr<const Packet> packet,
                              uint16_t channelFreqMhz,
                              WifiTxVector txVector,
                              MpduInfo mpduInfo,
                              uint16_t staId);

} // namespace ns3

#endif // SCENARIO1_MAC_HOP_MEASUREMENT_H
