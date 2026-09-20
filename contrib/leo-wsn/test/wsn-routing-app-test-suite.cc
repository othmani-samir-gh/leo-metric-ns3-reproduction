#include "ns3/leo-wsn-module.h"
#include "ns3/simulator.h"
#include "ns3/test.h"
#include "ns3/mobility-module.h"
#include "ns3/node-container.h"

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
static DeliveryContextSuite g_deliveryContextSuite;

} // namespace leo
} // namespace ns3
