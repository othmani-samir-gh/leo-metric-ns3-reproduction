#include "ns3/leo-wsn-module.h"
#include "ns3/simulator.h"
#include "ns3/test.h"
#include "ns3/mobility-module.h"
#include "ns3/node-container.h"
#include "ns3/rng-seed-manager.h"

#include <limits>

namespace ns3
{
namespace leo
{

class WsnRoutingAppTestPeer
{
  public:
    static NeighborEntry& Neighbor(WsnRoutingApp& app, uint32_t id)
    {
        return app.m_neighborTable.Get(Ipv4Address(id));
    }

    static bool HasValidRoute(const WsnRoutingApp& app, uint32_t target)
    {
        auto it = app.m_routingTable.find(target);
        return it != app.m_routingTable.end() && it->second.valid;
    }

    static void SetValidRoute(WsnRoutingApp& app, uint32_t target, uint32_t nextHop)
    {
        auto& route = app.m_routingTable[target];
        route.valid = true;
        route.nextHopId = nextHop;
        route.metric = 0.0;
        route.hopCount = 1;
    }

    static bool HasPendingDiscovery(const WsnRoutingApp& app, uint32_t target)
    {
        return app.m_pendingDiscoveryFloodId.find(target) != app.m_pendingDiscoveryFloodId.end();
    }

    static uint32_t PendingDiscoveryFloodId(const WsnRoutingApp& app, uint32_t target)
    {
        auto it = app.m_pendingDiscoveryFloodId.find(target);
        return it == app.m_pendingDiscoveryFloodId.end() ? 0u : it->second;
    }

    static void SetPendingDiscovery(WsnRoutingApp& app, uint32_t target, uint32_t floodId)
    {
        app.m_pendingDiscoveryFloodId[target] = floodId;
    }

    static void HandlePathDiscovery(WsnRoutingApp& app,
                                    WsnHeader hdr,
                                    uint32_t fromId,
                                    const WsnLinkInfoTag& tag)
    {
        app.HandlePathDiscovery(hdr, fromId, tag);
    }

    static void HandlePathDiscoveryReply(WsnRoutingApp& app,
                                         WsnHeader hdr,
                                         uint32_t fromId,
                                         const WsnLinkInfoTag& tag)
    {
        app.HandlePathDiscoveryReply(hdr, fromId, tag);
    }

    static void HandlePingReply(WsnRoutingApp& app,
                                WsnHeader hdr,
                                uint32_t fromId,
                                const WsnLinkInfoTag& tag)
    {
        app.HandlePingReply(hdr, fromId, tag);
    }

    static EventId PingTimeoutEvent(const WsnRoutingApp& app)
    {
        return app.m_pingTimeoutEvents.empty() ? EventId() : app.m_pingTimeoutEvents.begin()->second;
    }

    static void InjectFinalPendingAck(WsnRoutingApp& app,
                                      const std::string& key,
                                      uint32_t nextHopId)
    {
        WsnRoutingApp::PendingUnicast pu;
        pu.nextHopId = nextHopId;
        pu.attempt = app.m_macMaxRetries;
        app.m_pendingAcks[key] = pu;
    }

    static void FireAckTimeout(WsnRoutingApp& app, const std::string& key)
    {
        app.OnAckTimeout(key);
    }

    static void SetGlobalFrameCounter(uint64_t value)
    {
        WsnRoutingApp::s_globalFrameCounter = value;
    }

    static uint64_t GlobalFrameCounter()
    {
        return WsnRoutingApp::s_globalFrameCounter;
    }

    static void SendReliable(WsnRoutingApp& app,
                             WsnHeader hdr,
                             uint32_t nextHopId,
                             uint8_t attempt = 0,
                             uint32_t payloadBytes = 0)
    {
        app.SendUnicastReliable(hdr, nextHopId, attempt, payloadBytes);
    }

    static void Receive(WsnRoutingApp& app,
                        Ptr<Packet> packet,
                        Mac48Address from,
                        const WsnLinkInfoTag& tag)
    {
        app.OnReceive(packet, from, tag);
    }

    static LinkMetricResult LinkMetric(WsnRoutingApp& app, uint32_t neighborId)
    {
        return app.LinkMetricTo(neighborId);
    }
};

static Ptr<WsnRoutingApp>
MakeLeoApp(uint32_t nodeId, bool gateway = false)
{
    auto dev = CreateObject<WsnNetDevice>();
    dev->SetNodeId(nodeId);
    RadioParameters radio;
    auto app = CreateObject<WsnRoutingApp>();
    app->Configure(dev, nodeId, gateway, MetricType::LEO, radio, AtpcMode::WITH_FEEDBACK);
    return app;
}

class LinkUsableEnforcementTest : public TestCase
{
  public:
    LinkUsableEnforcementTest()
        : TestCase("LEO one-way link rejection must block discovery route installation")
    {
    }

  private:
    void DoRun() override
    {
        auto app = MakeLeoApp(2);
        auto& e = WsnRoutingAppTestPeer::Neighbor(*app, 1);
        e.maxTxPowerDbm = 8.0;
        e.maxTxPowerKnown = true;
        e.linkSignalLossDb = -20.0;
        e.lslKnown = true;
        e.pTxRequestedByNeighborDbm = 30.0;
        e.pTxRequestedByNeighborKnown = true;

        RadioParameters radio;
        LeoMetric metric;
        NS_TEST_ASSERT_MSG_EQ(metric.ComputeLinkMetric(e, radio).linkUsable,
                              false,
                              "fixture must be rejected by Eq.12-13");

        WsnHeader hdr;
        hdr.type = FrameType::PATH_DISCOVERY;
        hdr.originatorId = 0;
        hdr.targetId = 2;
        hdr.floodId = 11;
        hdr.prevHopId = 1;

        WsnLinkInfoTag tag;
        WsnRoutingAppTestPeer::HandlePathDiscovery(*app, hdr, 1, tag);

        NS_TEST_EXPECT_MSG_EQ(WsnRoutingAppTestPeer::HasValidRoute(*app, 0),
                              false,
                              "unusable link must not install reverse route");
        Simulator::Destroy();
    }
};

class DiscoveryTimeoutTest : public TestCase
{
  public:
    DiscoveryTimeoutTest()
        : TestCase("failed path discovery must expire and become restartable")
    {
    }

  private:
    void DoRun() override
    {
        auto app = MakeLeoApp(0, true);
        app->StartPathDiscovery(2);
        NS_TEST_ASSERT_MSG_EQ(WsnRoutingAppTestPeer::HasPendingDiscovery(*app, 2),
                              true,
                              "discovery should begin pending");

        Simulator::Stop(Seconds(1.1));
        Simulator::Run();

        NS_TEST_EXPECT_MSG_EQ(WsnRoutingAppTestPeer::HasPendingDiscovery(*app, 2),
                              false,
                              "failed discovery must not remain pending forever");
        Simulator::Destroy();
    }
};

class StaleDiscoveryReplyTest : public TestCase
{
  public:
    StaleDiscoveryReplyTest()
        : TestCase("stale discovery reply must not complete a newer flood transaction")
    {
    }

  private:
    void DoRun() override
    {
        auto app = MakeLeoApp(0, true);
        WsnRoutingAppTestPeer::SetPendingDiscovery(*app, 2, 7);

        WsnHeader stale;
        stale.type = FrameType::PATH_DISCOVERY_REPLY;
        stale.originatorId = 0;
        stale.targetId = 2;
        stale.floodId = 6;

        WsnLinkInfoTag tag;
        WsnRoutingAppTestPeer::HandlePathDiscoveryReply(*app, stale, 1, tag);

        NS_TEST_EXPECT_MSG_EQ(WsnRoutingAppTestPeer::HasPendingDiscovery(*app, 2),
                              true,
                              "stale reply must not clear current pending discovery");
        NS_TEST_EXPECT_MSG_EQ(WsnRoutingAppTestPeer::PendingDiscoveryFloodId(*app, 2),
                              7u,
                              "current flood id must be preserved");
        NS_TEST_EXPECT_MSG_EQ(WsnRoutingAppTestPeer::HasValidRoute(*app, 2),
                              false,
                              "stale reply must not install a forward route");
        Simulator::Destroy();
    }
};

class StalePingReplyTest : public TestCase
{
  public:
    StalePingReplyTest()
        : TestCase("unmatched ping reply must not cancel the active ping timeout")
    {
    }

  private:
    void DoRun() override
    {
        auto app = MakeLeoApp(0, true);
        WsnRoutingAppTestPeer::SetValidRoute(*app, 2, 1);
        app->StartPingCampaign({2});

        Simulator::Stop(NanoSeconds(1));
        Simulator::Run();

        NS_TEST_ASSERT_MSG_EQ(WsnRoutingAppTestPeer::PingTimeoutEvent(*app).IsPending(),
                              true,
                              "fixture must have an active ping timeout");

        WsnHeader stale;
        stale.type = FrameType::PING_REPLY;
        stale.originatorId = 0;
        stale.targetId = 2;
        stale.seq = 999;

        WsnLinkInfoTag tag;
        WsnRoutingAppTestPeer::HandlePingReply(*app, stale, 1, tag);

        NS_TEST_EXPECT_MSG_EQ(WsnRoutingAppTestPeer::PingTimeoutEvent(*app).IsPending(),
                              true,
                              "stale reply must leave the current timeout pending");
        Simulator::Destroy();
    }
};

class MatchingPingReplyTest : public TestCase
{
  public:
    MatchingPingReplyTest()
        : TestCase("matching ping reply cancels its timeout")
    {
    }

  private:
    void DoRun() override
    {
        auto app = MakeLeoApp(0, true);
        WsnRoutingAppTestPeer::SetValidRoute(*app, 2, 1);
        app->StartPingCampaign({2});

        Simulator::Stop(NanoSeconds(1));
        Simulator::Run();

        WsnHeader reply;
        reply.type = FrameType::PING_REPLY;
        reply.originatorId = 0;
        reply.targetId = 2;
        reply.seq = 0;

        WsnLinkInfoTag tag;
        WsnRoutingAppTestPeer::HandlePingReply(*app, reply, 1, tag);

        NS_TEST_EXPECT_MSG_EQ(WsnRoutingAppTestPeer::PingTimeoutEvent(*app).IsPending(),
                              false,
                              "matching reply must cancel the active timeout");
        Simulator::Destroy();
    }
};


class ArqRouteInvalidationTest : public TestCase
{
  public:
    ArqRouteInvalidationTest()
        : TestCase("final ARQ failure invalidates every route using the failed next hop")
    {
    }

  private:
    void DoRun() override
    {
        auto app = MakeLeoApp(0, true);
        WsnRoutingAppTestPeer::SetValidRoute(*app, 10, 1);
        WsnRoutingAppTestPeer::SetValidRoute(*app, 11, 1);
        WsnRoutingAppTestPeer::SetValidRoute(*app, 12, 2);

        WsnRoutingAppTestPeer::InjectFinalPendingAck(*app, "final-failure", 1);
        WsnRoutingAppTestPeer::FireAckTimeout(*app, "final-failure");

        NS_TEST_EXPECT_MSG_EQ(WsnRoutingAppTestPeer::HasValidRoute(*app, 10),
                              false,
                              "route 10 depends on failed next hop 1");
        NS_TEST_EXPECT_MSG_EQ(WsnRoutingAppTestPeer::HasValidRoute(*app, 11),
                              false,
                              "route 11 depends on failed next hop 1");
        NS_TEST_EXPECT_MSG_EQ(WsnRoutingAppTestPeer::HasValidRoute(*app, 12),
                              true,
                              "unrelated route through next hop 2 must remain valid");
        Simulator::Destroy();
    }
};

class FrameCounterRunResetTest : public TestCase
{
  public:
    FrameCounterRunResetTest()
        : TestCase("global frame safety counter resets at Simulator::Destroy between runs")
    {
    }

  private:
    void DoRun() override
    {
        auto app = MakeLeoApp(0, true);
        WsnRoutingAppTestPeer::SetGlobalFrameCounter(1234);
        Simulator::Destroy();
        NS_TEST_EXPECT_MSG_EQ(WsnRoutingAppTestPeer::GlobalFrameCounter(),
                              0u,
                              "a new independent run must not inherit the previous run frame count");
    }
};

class LifecycleCycleCleanupTest : public TestCase
{
  public:
    LifecycleCycleCleanupTest()
        : TestCase("Dispose breaks WsnChannel <-> WsnNetDevice ownership cycle")
    {
    }

  private:
    void DoRun() override
    {
        auto channel = CreateObject<WsnChannel>();
        auto dev = CreateObject<WsnNetDevice>();
        dev->SetChannel(channel);
        channel->Add(dev);

        channel->Dispose();

        NS_TEST_EXPECT_MSG_EQ(channel->GetNDevices(),
                              0u,
                              "disposed channel must release owned devices");
        NS_TEST_EXPECT_MSG_EQ(dev->GetChannel() == nullptr,
                              true,
                              "channel disposal must clear device back-reference");

        dev->Dispose();
        Simulator::Destroy();
    }
};

class DeliveryContextTest : public TestCase
{
  public:
    DeliveryContextTest()
        : TestCase("channel delivery executes under receiver node context")
    {
    }

  private:
    void OnReceive(Ptr<Packet>, Mac48Address, WsnLinkInfoTag)
    {
        ++m_deliveries;
        if (Simulator::GetContext() != m_receiverNodeId)
        {
            ++m_badContexts;
        }
    }

    void DoRun() override
    {
        RngSeedManager::SetSeed(1);
        RngSeedManager::SetRun(1);

        NodeContainer nodes;
        nodes.Create(2);
        for (uint32_t i = 0; i < 2; ++i)
        {
            auto mm = CreateObject<ConstantPositionMobilityModel>();
            mm->SetPosition(Vector(i * 0.01, 0.0, 0.0));
            nodes.Get(i)->AggregateObject(mm);
        }

        auto channel = CreateObject<WsnChannel>();
        channel->SetBackgroundNoiseDbm(-200.0);

        auto sender = CreateObject<WsnNetDevice>();
        sender->SetNodeId(nodes.Get(0)->GetId());
        sender->SetAddress(Mac48Address::Allocate());
        sender->SetChannel(channel);
        channel->Add(sender);

        auto receiver = CreateObject<WsnNetDevice>();
        receiver->SetNodeId(nodes.Get(1)->GetId());
        receiver->SetAddress(Mac48Address::Allocate());
        receiver->SetChannel(channel);
        receiver->SetReceiveCallback(MakeCallback(&DeliveryContextTest::OnReceive, this));
        channel->Add(receiver);
        m_receiverNodeId = receiver->GetNodeId();

        for (uint32_t i = 0; i < 20; ++i)
        {
            channel->SendUnicast(sender, Create<Packet>(1), 8.0, receiver->GetNodeId());
        }
        Simulator::Run();

        NS_TEST_ASSERT_MSG_GT(m_deliveries, 0u, "fixture must deliver at least one packet");
        NS_TEST_EXPECT_MSG_EQ(m_badContexts,
                              0u,
                              "all deliveries must execute with receiver node context");

        channel->Dispose();
        sender->Dispose();
        receiver->Dispose();
        Simulator::Destroy();
    }

    uint32_t m_receiverNodeId{std::numeric_limits<uint32_t>::max()};
    uint32_t m_deliveries{0};
    uint32_t m_badContexts{0};
};


class PhysicalTxQuantizationTest : public TestCase
{
  public:
    PhysicalTxQuantizationTest()
        : TestCase("physical nRF52840 TX quantization is independent of energy-accounting mode")
    {
    }

  private:
    void Exercise(bool useNrfEnergy)
    {
        auto dev = CreateObject<WsnNetDevice>();
        dev->SetNodeId(0);

        RadioParameters radio;
        radio.useNrf52840Energy = useNrfEnergy;

        auto app = CreateObject<WsnRoutingApp>();
        app->Configure(dev, 0, true, MetricType::LEO, radio, AtpcMode::WITH_FEEDBACK);

        auto& neighbor = WsnRoutingAppTestPeer::Neighbor(*app, 1);
        neighbor.reqTxPowerKnown = true;
        neighbor.reqTxPowerDbm = 3.2; // nRF52840 must realize this as +4 dBm

        WsnHeader hdr;
        hdr.type = FrameType::PING;
        hdr.originatorId = 0;
        hdr.targetId = 1;
        hdr.seq = useNrfEnergy ? 2 : 1;

        WsnRoutingAppTestPeer::SendReliable(*app, hdr, 1);

        NS_TEST_EXPECT_MSG_EQ_TOL(dev->GetTxPowerDbm(),
                                  4.0,
                                  1e-12,
                                  "requested 3.2 dBm must realize to the next supported +4 dBm level");

        app->Dispose();
        dev->Dispose();
        Simulator::Destroy();
    }

    void DoRun() override
    {
        Exercise(false);
        Exercise(true);
    }
};

class MetricRealizedTxPowerTest : public TestCase
{
  public:
    MetricRealizedTxPowerTest()
        : TestCase("LEO Eq.14 uses the same realized TX power as the physical radio")
    {
    }

  private:
    void DoRun() override
    {
        RadioParameters radio;
        radio.useNrf52840Energy = false;

        NeighborEntry e;
        e.reqTxPowerKnown = true;
        e.reqTxPowerDbm = 3.2;
        e.rRxKnown = true;
        e.rRx = 0.0;
        e.rTxKnown = true;
        e.rTx = 0.0;

        LeoMetric metric;
        LinkMetricResult result = metric.ComputeLinkMetric(e, radio);

        const double realizedDbm = 4.0;
        const double expected = LeoMetric::LinkPowerMw(0.0, realizedDbm, radio);

        NS_TEST_EXPECT_MSG_EQ_TOL(result.value,
                                  expected,
                                  1e-12,
                                  "metric P_TX must use the +4 dBm level actually transmitted by nRF52840");
    }
};


class AckPhysicalTxQuantizationTest : public TestCase
{
  public:
    AckPhysicalTxQuantizationTest()
        : TestCase("ACK TX uses discrete nRF52840 power even with simplified energy accounting")
    {
    }

  private:
    void DoRun() override
    {
        auto dev = CreateObject<WsnNetDevice>();
        dev->SetNodeId(0);

        RadioParameters radio;
        radio.useNrf52840Energy = false;

        auto app = CreateObject<WsnRoutingApp>();
        app->Configure(dev, 0, true, MetricType::LEO, radio, AtpcMode::WITH_FEEDBACK);

        auto& neighbor = WsnRoutingAppTestPeer::Neighbor(*app, 1);
        neighbor.reqTxPowerKnown = true;
        neighbor.reqTxPowerDbm = 3.2;

        WsnHeader ping;
        ping.type = FrameType::PING;
        ping.origType = static_cast<uint8_t>(FrameType::PING);
        ping.originatorId = 1;
        ping.targetId = 0;
        ping.seq = 77;
        ping.prevHopId = 1;
        ping.intendedNextHopId = 0;

        Ptr<Packet> packet = Create<Packet>();
        packet->AddHeader(ping);
        WsnLinkInfoTag tag;
        tag.rssiDbm = -50.0;
        tag.snrDb = 66.0;
        tag.txPowerDbm = 4.0;

        WsnRoutingAppTestPeer::Receive(*app, packet, Mac48Address(), tag);

        NS_TEST_EXPECT_MSG_EQ_TOL(dev->GetTxPowerDbm(),
                                  4.0,
                                  1e-12,
                                  "ACK requested at 3.2 dBm must realize at +4 dBm");

        app->Dispose();
        dev->Dispose();
        Simulator::Destroy();
    }
};

class FailedRxEnergyTest : public TestCase
{
  public:
    FailedRxEnergyTest()
        : TestCase("failed packet reception still consumes receiver energy")
    {
    }

  private:
    void DoRun() override
    {
        RngSeedManager::SetSeed(11);
        RngSeedManager::SetRun(1);

        NodeContainer nodes;
        nodes.Create(2);
        for (uint32_t i = 0; i < 2; ++i)
        {
            auto mm = CreateObject<ConstantPositionMobilityModel>();
            mm->SetPosition(Vector(i * 0.01, 0.0, 0.0));
            nodes.Get(i)->AggregateObject(mm);
        }

        auto channel = CreateObject<WsnChannel>();
        channel->SetBackgroundNoiseDbm(100.0); // force SNR below curve -> PRR=0

        auto sender = CreateObject<WsnNetDevice>();
        sender->SetNodeId(nodes.Get(0)->GetId());
        sender->SetAddress(Mac48Address::Allocate());
        sender->SetChannel(channel);
        channel->Add(sender);

        auto receiver = CreateObject<WsnNetDevice>();
        receiver->SetNodeId(nodes.Get(1)->GetId());
        receiver->SetAddress(Mac48Address::Allocate());
        receiver->SetChannel(channel);
        channel->Add(receiver);

        RadioParameters radio;
        radio.useNrf52840Energy = false;
        auto app = CreateObject<WsnRoutingApp>();
        app->Configure(receiver,
                       receiver->GetNodeId(),
                       false,
                       MetricType::HOP_COUNT,
                       radio,
                       AtpcMode::WITH_FEEDBACK);
        app->SetPhyTiming(1.0e6, 100.0e-6);

        WsnHeader hdr;
        hdr.type = FrameType::PING;
        hdr.origType = static_cast<uint8_t>(FrameType::PING);
        hdr.originatorId = sender->GetNodeId();
        hdr.targetId = receiver->GetNodeId();
        hdr.prevHopId = sender->GetNodeId();
        hdr.intendedNextHopId = receiver->GetNodeId();

        Ptr<Packet> packet = Create<Packet>(1);
        packet->AddHeader(hdr);
        const uint32_t frameBytes = packet->GetSize();
        const double expected =
            radio.rxPowerPenaltyMw * ((8.0 * frameBytes) / 1.0e6 + 100.0e-6);

        channel->SendUnicast(sender, packet, 8.0, receiver->GetNodeId());
        Simulator::Run();

        NS_TEST_EXPECT_MSG_EQ_TOL(receiver->GetEnergyConsumedMWs(),
                                  expected,
                                  1e-12,
                                  "failed decode must still pay one RX-attempt energy cost");

        app->Dispose();
        channel->Dispose();
        sender->Dispose();
        receiver->Dispose();
        Simulator::Destroy();
    }
};

class AckTimeoutListeningEnergyTest : public TestCase
{
  public:
    AckTimeoutListeningEnergyTest()
        : TestCase("missing ACK charges the sender for the ACK listening timeout")
    {
    }

  private:
    void DoRun() override
    {
        auto dev = CreateObject<WsnNetDevice>();
        dev->SetNodeId(0);

        RadioParameters radio;
        radio.useNrf52840Energy = false;
        radio.rxPowerPenaltyMw = 1.0;

        auto app = CreateObject<WsnRoutingApp>();
        app->Configure(dev, 0, true, MetricType::HOP_COUNT, radio, AtpcMode::WITH_FEEDBACK);
        app->SetTiming(1.0e-3, 1.0e-3, 0); // no retransmission after the first timeout
        app->SetPhyTiming(1.0e6, 100.0e-6);

        WsnHeader hdr;
        hdr.type = FrameType::PING;
        hdr.origType = static_cast<uint8_t>(FrameType::PING);
        hdr.originatorId = 0;
        hdr.targetId = 1;
        hdr.seq = 1;

        WsnRoutingAppTestPeer::SendReliable(*app, hdr, 1);
        const double energyAfterTx = app->GetStats().energyConsumedMWs;

        Simulator::Stop(Seconds(0.051));
        Simulator::Run();

        const double listenEnergy =
            app->GetStats().energyConsumedMWs - energyAfterTx;
        NS_TEST_EXPECT_MSG_EQ_TOL(listenEnergy,
                                  0.05,
                                  1e-12,
                                  "50 ms ACK timeout at 1 mW RX must cost 0.05 mWs");

        app->Dispose();
        dev->Dispose();
        Simulator::Destroy();
    }
};


class EffectiveSnrLqiFallbackTest : public TestCase
{
  public:
    EffectiveSnrLqiFallbackTest()
        : TestCase("LQI first-contact fallback uses channel effective SNR including interference")
    {
    }

  private:
    void DoRun() override
    {
        auto dev = CreateObject<WsnNetDevice>();
        dev->SetNodeId(2);

        RadioParameters radio;
        radio.backgroundNoiseDbm = -116.0;

        auto app = CreateObject<WsnRoutingApp>();
        app->Configure(dev, 2, false, MetricType::ZIGBEE_LQI, radio, AtpcMode::WITH_FEEDBACK);

        WsnHeader hdr;
        hdr.type = FrameType::PATH_DISCOVERY; // broadcast, no ACK side effect
        hdr.originatorId = 0;
        hdr.targetId = 99;
        hdr.floodId = 1;
        hdr.prevHopId = 1;

        Ptr<Packet> packet = Create<Packet>();
        packet->AddHeader(hdr);

        WsnLinkInfoTag tag;
        tag.rssiDbm = -80.0;  // reconstructed SNR from RSSI-noise would be +36 dB
        tag.snrDb = 5.0;      // authoritative effective SNR after channel penalty
        tag.txPowerDbm = 4.0;

        WsnRoutingAppTestPeer::Receive(*app, packet, Mac48Address(), tag);

        const double expected =
            ZigbeeLqiMetric::LinkCostFromDeliveryProbability(PrrFromSnrDb(tag.snrDb));
        const double unpenalized =
            ZigbeeLqiMetric::LinkCostFromDeliveryProbability(
                PrrFromSnrDb(tag.rssiDbm - radio.backgroundNoiseDbm));

        NS_TEST_ASSERT_MSG_NE(expected,
                              unpenalized,
                              "fixture must distinguish effective and reconstructed SNR");

        LinkMetricResult result = WsnRoutingAppTestPeer::LinkMetric(*app, 1);
        NS_TEST_EXPECT_MSG_EQ_TOL(result.value,
                                  expected,
                                  1e-12,
                                  "first-contact LQI must use the effective SNR delivered by the channel");

        app->Dispose();
        dev->Dispose();
        Simulator::Destroy();
    }
};


class Eq17PolicyTest : public TestCase
{
  public:
    Eq17PolicyTest()
        : TestCase("Eq17 bounded and literal reconstruction policies are explicit and reproducible")
    {
    }

  private:
    void DoRun() override
    {
        RadioParameters bounded;
        bounded.rMax = 4;
        bounded.retransmissionDeficiencySpanDb = 15.0;
        bounded.boundEq17ToRMax = true;

        RadioParameters literal = bounded;
        literal.boundEq17ToRMax = false;

        const double boundedValue =
            LeoMetric::RetransmissionsFromPowerDeficiency(15.0, 0.0, bounded);
        const double literalValue =
            LeoMetric::RetransmissionsFromPowerDeficiency(15.0, 0.0, literal);

        NS_TEST_EXPECT_MSG_EQ_TOL(boundedValue,
                                  4.0,
                                  1e-12,
                                  "bounded v1.0.0 policy must cap R_D at R_max");
        NS_TEST_EXPECT_MSG_EQ_TOL(literalValue,
                                  40.0,
                                  1e-12,
                                  "literal Eq.17 at 15 dB deficiency and R_max=4 equals 40");
    }
};


class ChannelStreamIdentityTest : public TestCase
{
  public:
    ChannelStreamIdentityTest()
        : TestCase("explicit channel RNG stream is stable against unrelated RNG allocation order")
    {
    }

  private:
    class Recorder
    {
      public:
        void OnSuccess(Ptr<Packet>, Mac48Address, WsnLinkInfoTag)
        {
            outcomes.push_back(1);
        }

        void OnFailure(Ptr<Packet>, Mac48Address, WsnLinkInfoTag)
        {
            outcomes.push_back(0);
        }

        std::vector<uint8_t> outcomes;
    };

    std::vector<uint8_t> RunSequence(uint32_t junkObjects)
    {
        RngSeedManager::SetSeed(12345);
        RngSeedManager::SetRun(7);

        std::vector<Ptr<UniformRandomVariable>> junk;
        for (uint32_t i = 0; i < junkObjects; ++i)
        {
            auto rv = CreateObject<UniformRandomVariable>();
            (void)rv->GetValue();
            junk.push_back(rv);
        }

        NodeContainer nodes;
        nodes.Create(2);
        for (uint32_t i = 0; i < 2; ++i)
        {
            auto mm = CreateObject<ConstantPositionMobilityModel>();
            mm->SetPosition(Vector(i * 1.0, 0.0, 0.0));
            nodes.Get(i)->AggregateObject(mm);
        }

        auto channel = CreateObject<WsnChannel>();
        channel->SetBackgroundNoiseDbm(-84.0); // 0 dBm TX at 1 m -> SNR ~20 dB, non-trivial PRR
        NS_TEST_EXPECT_MSG_EQ(channel->AssignStreams(77),
                              1,
                              "WsnChannel should consume exactly one RNG stream");

        auto sender = CreateObject<WsnNetDevice>();
        sender->SetNodeId(nodes.Get(0)->GetId());
        sender->SetAddress(Mac48Address::Allocate());
        sender->SetChannel(channel);
        channel->Add(sender);

        Recorder recorder;
        auto receiver = CreateObject<WsnNetDevice>();
        receiver->SetNodeId(nodes.Get(1)->GetId());
        receiver->SetAddress(Mac48Address::Allocate());
        receiver->SetChannel(channel);
        receiver->SetReceiveCallback(MakeCallback(&Recorder::OnSuccess, &recorder));
        receiver->SetReceiveFailureCallback(MakeCallback(&Recorder::OnFailure, &recorder));
        channel->Add(receiver);

        for (uint32_t i = 0; i < 64; ++i)
        {
            channel->SendUnicast(sender, Create<Packet>(1), 0.0, receiver->GetNodeId());
        }
        Simulator::Run();

        auto result = recorder.outcomes;
        channel->Dispose();
        sender->Dispose();
        receiver->Dispose();
        Simulator::Destroy();
        return result;
    }

    void DoRun() override
    {
        const auto baseline = RunSequence(0);
        const auto perturbed = RunSequence(11);

        NS_TEST_EXPECT_MSG_EQ(baseline.size(),
                              64u,
                              "baseline fixture must record every channel roll");
        NS_TEST_EXPECT_MSG_EQ(perturbed.size(),
                              64u,
                              "perturbed fixture must record every channel roll");
        NS_TEST_EXPECT_MSG_EQ(baseline == perturbed,
                              true,
                              "explicit stream identity must make channel draws independent of unrelated RNG allocations");
    }
};

class LinkUsableSuite : public TestSuite
{
  public:
    LinkUsableSuite()
        : TestSuite("leo-r1-link-usable", Type::UNIT)
    {
        AddTestCase(new LinkUsableEnforcementTest(), TestCase::Duration::QUICK);
    }
};

class DiscoveryTimeoutSuite : public TestSuite
{
  public:
    DiscoveryTimeoutSuite()
        : TestSuite("leo-r1-discovery-timeout", Type::UNIT)
    {
        AddTestCase(new DiscoveryTimeoutTest(), TestCase::Duration::QUICK);
    }
};

class StaleDiscoveryReplySuite : public TestSuite
{
  public:
    StaleDiscoveryReplySuite()
        : TestSuite("leo-r1-stale-discovery-reply", Type::UNIT)
    {
        AddTestCase(new StaleDiscoveryReplyTest(), TestCase::Duration::QUICK);
    }
};

class StalePingReplySuite : public TestSuite
{
  public:
    StalePingReplySuite()
        : TestSuite("leo-r1-stale-ping-reply", Type::UNIT)
    {
        AddTestCase(new StalePingReplyTest(), TestCase::Duration::QUICK);
    }
};

class MatchingPingReplySuite : public TestSuite
{
  public:
    MatchingPingReplySuite()
        : TestSuite("leo-r1-matching-ping-reply", Type::UNIT)
    {
        AddTestCase(new MatchingPingReplyTest(), TestCase::Duration::QUICK);
    }
};

static LinkUsableSuite g_linkUsableSuite;
static DiscoveryTimeoutSuite g_discoveryTimeoutSuite;
static StaleDiscoveryReplySuite g_staleDiscoveryReplySuite;
static StalePingReplySuite g_stalePingReplySuite;
class ArqRouteInvalidationSuite : public TestSuite
{
  public:
    ArqRouteInvalidationSuite()
        : TestSuite("leo-r1-arq-route-invalidation", Type::UNIT)
    {
        AddTestCase(new ArqRouteInvalidationTest(), TestCase::Duration::QUICK);
    }
};

class FrameCounterResetSuite : public TestSuite
{
  public:
    FrameCounterResetSuite()
        : TestSuite("leo-r1-frame-counter-reset", Type::UNIT)
    {
        AddTestCase(new FrameCounterRunResetTest(), TestCase::Duration::QUICK);
    }
};

class LifecycleCleanupSuite : public TestSuite
{
  public:
    LifecycleCleanupSuite()
        : TestSuite("leo-r1-lifecycle-cleanup", Type::UNIT)
    {
        AddTestCase(new LifecycleCycleCleanupTest(), TestCase::Duration::QUICK);
    }
};

class DeliveryContextSuite : public TestSuite
{
  public:
    DeliveryContextSuite()
        : TestSuite("leo-r1-delivery-context", Type::UNIT)
    {
        AddTestCase(new DeliveryContextTest(), TestCase::Duration::QUICK);
    }
};

static MatchingPingReplySuite g_matchingPingReplySuite;
static ArqRouteInvalidationSuite g_arqRouteInvalidationSuite;
static FrameCounterResetSuite g_frameCounterResetSuite;
static LifecycleCleanupSuite g_lifecycleCleanupSuite;
class PhysicalTxQuantizationSuite : public TestSuite
{
  public:
    PhysicalTxQuantizationSuite()
        : TestSuite("leo-r2-physical-tx-quantization", Type::UNIT)
    {
        AddTestCase(new PhysicalTxQuantizationTest(), TestCase::Duration::QUICK);
    }
};

class MetricRealizedTxPowerSuite : public TestSuite
{
  public:
    MetricRealizedTxPowerSuite()
        : TestSuite("leo-r2-metric-realized-tx-power", Type::UNIT)
    {
        AddTestCase(new MetricRealizedTxPowerTest(), TestCase::Duration::QUICK);
    }
};

static DeliveryContextSuite g_deliveryContextSuite;
static PhysicalTxQuantizationSuite g_physicalTxQuantizationSuite;
class AckPhysicalTxQuantizationSuite : public TestSuite
{
  public:
    AckPhysicalTxQuantizationSuite()
        : TestSuite("leo-r2-ack-tx-quantization", Type::UNIT)
    {
        AddTestCase(new AckPhysicalTxQuantizationTest(), TestCase::Duration::QUICK);
    }
};

class FailedRxEnergySuite : public TestSuite
{
  public:
    FailedRxEnergySuite()
        : TestSuite("leo-r2-failed-rx-energy", Type::UNIT)
    {
        AddTestCase(new FailedRxEnergyTest(), TestCase::Duration::QUICK);
    }
};

class AckTimeoutListeningEnergySuite : public TestSuite
{
  public:
    AckTimeoutListeningEnergySuite()
        : TestSuite("leo-r2-ack-timeout-listening-energy", Type::UNIT)
    {
        AddTestCase(new AckTimeoutListeningEnergyTest(), TestCase::Duration::QUICK);
    }
};

static MetricRealizedTxPowerSuite g_metricRealizedTxPowerSuite;
static AckPhysicalTxQuantizationSuite g_ackPhysicalTxQuantizationSuite;
static FailedRxEnergySuite g_failedRxEnergySuite;
class EffectiveSnrLqiFallbackSuite : public TestSuite
{
  public:
    EffectiveSnrLqiFallbackSuite()
        : TestSuite("leo-r2-effective-snr-lqi", Type::UNIT)
    {
        AddTestCase(new EffectiveSnrLqiFallbackTest(), TestCase::Duration::QUICK);
    }
};

static AckTimeoutListeningEnergySuite g_ackTimeoutListeningEnergySuite;
class Eq17PolicySuite : public TestSuite
{
  public:
    Eq17PolicySuite()
        : TestSuite("leo-r2-eq17-policy", Type::UNIT)
    {
        AddTestCase(new Eq17PolicyTest(), TestCase::Duration::QUICK);
    }
};

static EffectiveSnrLqiFallbackSuite g_effectiveSnrLqiFallbackSuite;
class ChannelStreamIdentitySuite : public TestSuite
{
  public:
    ChannelStreamIdentitySuite()
        : TestSuite("leo-r3-channel-stream-identity", Type::UNIT)
    {
        AddTestCase(new ChannelStreamIdentityTest(), TestCase::Duration::QUICK);
    }
};

static Eq17PolicySuite g_eq17PolicySuite;
static ChannelStreamIdentitySuite g_channelStreamIdentitySuite;

} // namespace leo
} // namespace ns3
