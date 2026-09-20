#include "ns3/leo-wsn-module.h"
#include "ns3/simulator.h"
#include "ns3/test.h"

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
        return app.m_pendingDiscoveryFloodId.at(target);
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
        return app.m_timeoutEvent;
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

class WsnRoutingAppRegressionSuite : public TestSuite
{
  public:
    WsnRoutingAppRegressionSuite()
        : TestSuite("leo-wsn-routing-regression", Type::UNIT)
    {
        AddTestCase(new LinkUsableEnforcementTest(), TestCase::Duration::QUICK);
        AddTestCase(new DiscoveryTimeoutTest(), TestCase::Duration::QUICK);
        AddTestCase(new StaleDiscoveryReplyTest(), TestCase::Duration::QUICK);
        AddTestCase(new StalePingReplyTest(), TestCase::Duration::QUICK);
        AddTestCase(new MatchingPingReplyTest(), TestCase::Duration::QUICK);
    }
};

static WsnRoutingAppRegressionSuite g_wsnRoutingAppRegressionSuite;

} // namespace leo
} // namespace ns3
