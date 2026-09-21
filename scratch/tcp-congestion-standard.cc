/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Standalone TCP NewReno congestion-control teaching example.
 *
 * Topology:
 *
 *   TCP sender (n0) -------- 5 Mbps, 2 ms -------- TCP sink (n1)
 *                                                    + packet loss
 *
 * The example logs Slow Start, Congestion Avoidance, Additive Increase,
 * Multiplicative Decrease, ssthresh updates, retransmissions, RTT, and RTO.
 * It also creates a CSV file for utils/plot-tcp-congestion.py.
 *
 * Run:
 *   ./ns3 run "tcp-congestion-standard --detailedLog=false"
 *
 * Plot in the terminal:
 *   python3 utils/plot-tcp-congestion.py tcp-congestion-standard.csv
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

#include <fstream>
#include <iomanip>
#include <limits>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpCongestionStandardExample");

static uint32_t g_segmentSize = 0;
static uint32_t g_ssThresh = std::numeric_limits<uint32_t>::max();
static std::ofstream g_cwndCsv;

/** A small rate-controlled sender whose socket is visible for trace attachment. */
class TcpTrafficSource : public Application
{
  public:
    TcpTrafficSource() = default;
    ~TcpTrafficSource() override = default;

    static TypeId GetTypeId()
    {
        static TypeId tid = TypeId("TcpTrafficSource")
                                .SetParent<Application>()
                                .SetGroupName("Tutorial")
                                .AddConstructor<TcpTrafficSource>();
        return tid;
    }

    void Setup(Ptr<Socket> socket,
               const Address& peer,
               uint32_t packetSize,
               uint32_t packetCount,
               DataRate dataRate)
    {
        m_socket = socket;
        m_peer = peer;
        m_packetSize = packetSize;
        m_packetCount = packetCount;
        m_dataRate = dataRate;
    }

  private:
    void StartApplication() override
    {
        m_running = true;
        m_packetsSent = 0;
        m_socket->Bind();
        m_socket->Connect(m_peer);
        SendPacket();
    }

    void StopApplication() override
    {
        m_running = false;
        if (m_sendEvent.IsPending())
        {
            Simulator::Cancel(m_sendEvent);
        }
        if (m_socket)
        {
            m_socket->Close();
        }
    }

    void SendPacket()
    {
        m_socket->Send(Create<Packet>(m_packetSize));
        ++m_packetsSent;
        if (m_running && m_packetsSent < m_packetCount)
        {
            const Time interval =
                Seconds(m_packetSize * 8.0 / static_cast<double>(m_dataRate.GetBitRate()));
            m_sendEvent = Simulator::Schedule(interval, &TcpTrafficSource::SendPacket, this);
        }
    }

    Ptr<Socket> m_socket;
    Address m_peer;
    uint32_t m_packetSize{0};
    uint32_t m_packetCount{0};
    uint32_t m_packetsSent{0};
    DataRate m_dataRate{0};
    EventId m_sendEvent;
    bool m_running{false};
};

static void
RecordCwndChange(uint32_t oldCwnd,
                 uint32_t newCwnd,
                 const std::string& phase,
                 const std::string& action)
{
    const double oldMss = 1.0 * oldCwnd / g_segmentSize;
    const double newMss = 1.0 * newCwnd / g_segmentSize;
    const double ssThreshMss = 1.0 * g_ssThresh / g_segmentSize;

    g_cwndCsv << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ','
              << oldCwnd << ',' << newCwnd << ',' << std::setprecision(3) << oldMss << ',' << newMss
              << ',' << g_ssThresh << ',' << ssThreshMss << ',' << phase << ',' << action << '\n';
}

static void
CwndChange(uint32_t oldCwnd, uint32_t newCwnd)
{
    const double oldMss = 1.0 * oldCwnd / g_segmentSize;
    const double newMss = 1.0 * newCwnd / g_segmentSize;

    if (newCwnd < oldCwnd)
    {
        RecordCwndChange(oldCwnd, newCwnd, "LOSS_RECOVERY", "MULTIPLICATIVE_DECREASE");
        const double ratio = 1.0 * newCwnd / oldCwnd;
        NS_LOG_UNCOND("[TCP " << Simulator::Now().GetSeconds()
                              << "s] phase=LOSS_RECOVERY AIMD=MULTIPLICATIVE_DECREASE"
                              << " cwnd=" << oldCwnd << " -> " << newCwnd << " bytes ("
                              << std::fixed << std::setprecision(2) << oldMss << " -> " << newMss
                              << " MSS, ratio=" << ratio << ") ssthresh=" << g_ssThresh << " bytes");
        return;
    }

    if (oldCwnd < g_ssThresh)
    {
        RecordCwndChange(oldCwnd, newCwnd, "SLOW_START", "EXPONENTIAL_INCREASE");
        NS_LOG_UNCOND("[TCP " << Simulator::Now().GetSeconds()
                              << "s] phase=SLOW_START action=EXPONENTIAL_INCREASE"
                              << " cwnd=" << oldCwnd << " -> " << newCwnd << " bytes ("
                              << std::fixed << std::setprecision(2) << oldMss << " -> " << newMss
                              << " MSS) ssthresh=" << g_ssThresh << " bytes");
        if (newCwnd >= g_ssThresh)
        {
            NS_LOG_UNCOND("[TCP " << Simulator::Now().GetSeconds()
                                  << "s] PHASE_TRANSITION SLOW_START -> CONGESTION_AVOIDANCE");
        }
        return;
    }

    RecordCwndChange(oldCwnd, newCwnd, "CONGESTION_AVOIDANCE", "ADDITIVE_INCREASE");
    NS_LOG_UNCOND("[TCP " << Simulator::Now().GetSeconds()
                          << "s] phase=CONGESTION_AVOIDANCE AIMD=ADDITIVE_INCREASE"
                          << " cwnd=" << oldCwnd << " -> " << newCwnd << " bytes (" << std::fixed
                          << std::setprecision(2) << oldMss << " -> " << newMss
                          << " MSS) ssthresh=" << g_ssThresh << " bytes");
}

static void
SsThreshChange(uint32_t oldValue, uint32_t newValue)
{
    g_ssThresh = newValue;
    NS_LOG_UNCOND("[TCP " << Simulator::Now().GetSeconds()
                          << "s] LOSS_RESPONSE ssthresh=" << oldValue << " -> " << newValue
                          << " bytes; rule=max(2*MSS, 0.5*bytesInFlight)");
}

static void
CongStateChange(TcpSocketState::TcpCongState_t oldState, TcpSocketState::TcpCongState_t newState)
{
    NS_LOG_UNCOND("[TCP " << Simulator::Now().GetSeconds() << "s] congestion-state "
                          << TcpSocketState::TcpCongStateName[oldState] << " -> "
                          << TcpSocketState::TcpCongStateName[newState]);
}

static void
RttChange(Time oldValue, Time newValue)
{
    NS_LOG_UNCOND("[TCP " << Simulator::Now().GetSeconds() << "s] RTT "
                          << oldValue.GetMilliSeconds() << " -> " << newValue.GetMilliSeconds()
                          << " ms");
}

static void
RtoChange(Time oldValue, Time newValue)
{
    NS_LOG_UNCOND("[TCP " << Simulator::Now().GetSeconds() << "s] RTO "
                          << oldValue.GetMilliSeconds() << " -> " << newValue.GetMilliSeconds()
                          << " ms");
}

static void
BytesInFlightChange(uint32_t oldValue, uint32_t newValue)
{
    NS_LOG_UNCOND("[TCP " << Simulator::Now().GetSeconds() << "s] bytes-in-flight " << oldValue
                          << " -> " << newValue);
}

static void
Retransmission(Ptr<const Packet> packet,
               const TcpHeader& header,
               const Address&,
               const Address&,
               Ptr<const TcpSocketBase>)
{
    NS_LOG_UNCOND("[TCP " << Simulator::Now().GetSeconds()
                          << "s] RETRANSMIT seq=" << header.GetSequenceNumber()
                          << " payload=" << packet->GetSize() << " bytes");
}

static void
RxDrop(Ptr<const Packet> packet)
{
    NS_LOG_UNCOND("[LINK " << Simulator::Now().GetSeconds() << "s] DROP size=" << packet->GetSize()
                           << " bytes; TCP will infer loss from missing ACKs");
}

int
main(int argc, char* argv[])
{
    bool detailedLog = false;
    double errorRate = 0.00001;
    uint32_t packetSize = 1040;
    uint32_t packetCount = 1000;
    std::string applicationRate = "1Mbps";
    std::string csvFile = "tcp-congestion-standard.csv";

    CommandLine cmd(__FILE__);
    cmd.AddValue("detailedLog", "Log RTT, RTO, bytes in flight, states, and retransmissions", detailedLog);
    cmd.AddValue("errorRate", "Receive error probability used by RateErrorModel", errorRate);
    cmd.AddValue("packetSize", "Application packet size in bytes", packetSize);
    cmd.AddValue("packetCount", "Number of application packets", packetCount);
    cmd.AddValue("applicationRate", "Application sending rate", applicationRate);
    cmd.AddValue("csvFile", "Congestion-window CSV output filename", csvFile);
    cmd.Parse(argc, argv);

    if (detailedLog)
    {
        LogComponentEnable("TcpLinuxReno", LOG_LEVEL_DEBUG);
        LogComponentEnable("TcpSocketBase", LOG_LEVEL_INFO);
    }

    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno"));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(1));
    Config::SetDefault("ns3::TcpL4Protocol::RecoveryType",
                       TypeIdValue(TypeId::LookupByName("ns3::TcpClassicRecovery")));

    NodeContainer nodes;
    nodes.Create(2);

    PointToPointHelper link;
    link.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    link.SetChannelAttribute("Delay", StringValue("2ms"));
    NetDeviceContainer devices = link.Install(nodes);

    Ptr<RateErrorModel> receiveErrors = CreateObject<RateErrorModel>();
    receiveErrors->SetAttribute("ErrorRate", DoubleValue(errorRate));
    devices.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(receiveErrors));
    devices.Get(1)->TraceConnectWithoutContext("PhyRxDrop", MakeCallback(&RxDrop));

    InternetStackHelper internet;
    internet.Install(nodes);

    Ipv4AddressHelper addresses;
    addresses.SetBase("10.1.1.0", "255.255.255.252");
    Ipv4InterfaceContainer interfaces = addresses.Assign(devices);

    constexpr uint16_t port = 8080;
    PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApps = sinkHelper.Install(nodes.Get(1));
    sinkApps.Start(Seconds(0));
    sinkApps.Stop(Seconds(20));

    Ptr<Socket> senderSocket = Socket::CreateSocket(nodes.Get(0), TcpSocketFactory::GetTypeId());
    UintegerValue segmentSize;
    senderSocket->GetAttribute("SegmentSize", segmentSize);
    g_segmentSize = segmentSize.Get();

    g_cwndCsv.open(csvFile, std::ios::out | std::ios::trunc);
    NS_ABORT_MSG_IF(!g_cwndCsv.is_open(), "Cannot open CSV file: " << csvFile);
    g_cwndCsv << "time_s,old_cwnd_bytes,new_cwnd_bytes,old_cwnd_mss,new_cwnd_mss,"
                 "ssthresh_bytes,ssthresh_mss,phase,action\n";

    senderSocket->TraceConnectWithoutContext("CongestionWindow", MakeCallback(&CwndChange));
    senderSocket->TraceConnectWithoutContext("SlowStartThreshold", MakeCallback(&SsThreshChange));
    if (detailedLog)
    {
        senderSocket->TraceConnectWithoutContext("CongState", MakeCallback(&CongStateChange));
        senderSocket->TraceConnectWithoutContext("RTT", MakeCallback(&RttChange));
        senderSocket->TraceConnectWithoutContext("RTO", MakeCallback(&RtoChange));
        senderSocket->TraceConnectWithoutContext("BytesInFlight",
                                                 MakeCallback(&BytesInFlightChange));
        senderSocket->TraceConnectWithoutContext("Retransmission",
                                                 MakeCallback(&Retransmission));
    }

    Ptr<TcpTrafficSource> source = CreateObject<TcpTrafficSource>();
    source->Setup(senderSocket,
                  InetSocketAddress(interfaces.GetAddress(1), port),
                  packetSize,
                  packetCount,
                  DataRate(applicationRate));
    nodes.Get(0)->AddApplication(source);
    source->SetStartTime(Seconds(1));
    source->SetStopTime(Seconds(20));

    std::cout << "\n=== Standard TCP congestion-control example ==="
              << "\nAlgorithm       : TcpNewReno"
              << "\nInitial cwnd    : 1 MSS"
              << "\nMSS             : " << g_segmentSize << " bytes"
              << "\nLink            : 5 Mbps, 2 ms one-way delay"
              << "\nApplication     : " << packetCount << " x " << packetSize << " bytes at "
              << applicationRate << "\nReceive errors  : " << errorRate
              << "\nCSV output      : " << csvFile
              << "\nPhases          : Slow Start -> Congestion Avoidance -> Loss Recovery\n\n";

    Simulator::Stop(Seconds(20));
    Simulator::Run();

    Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApps.Get(0));
    std::cout << "\nSimulation summary: received " << sink->GetTotalRx() << " bytes; CSV="
              << csvFile << '\n';

    g_cwndCsv.close();
    Simulator::Destroy();
    return 0;
}
