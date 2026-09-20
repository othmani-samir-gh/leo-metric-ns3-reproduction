/*
 * wsn-routing-app.h
 *
 * Reproduces, at application layer, the MeshNet routing behaviour
 * described in Sections 3.3 and 4 of the paper:
 *
 *  - Flooding-based bidirectional route discovery (Section 3.3, the
 *    paragraph beginning "The LEO metric assumes that all links on the
 *    route have a similar metric in both directions..."): a single flood
 *    from source to destination, followed by one unicast reply retracing
 *    the discovered route, establishing both directions without a second
 *    flood.
 *  - Metric-dependent relay behaviour: for Hop-count/ZigBee-LQI a node
 *    relays a given discovery flood only once (first-seen suppression).
 *    For LEO, a node MAY relay the same flood a second time if it later
 *    receives a copy with a strictly better accumulated metric ("some
 *    nodes can relay the path discovery packet multiple times using the
 *    LEO metric ... if a node receives the same path discovery packet for
 *    the second time with a better metric than the previous one",
 *    Section 4).
 *  - A gateway ("central node") pings every other node in the network at
 *    1 s intervals, 10 pings per node (Section 4), triggering path
 *    discovery/repair on loss.
 *  - Per-node cumulative energy (mWs) and per-destination ping-timeout
 *    counters are exposed so a scratch script can reproduce the plots of
 *    Figs. 5-10.
 *
 * As documented in wsn-channel.h, physical-layer timing/energy constants
 * that the paper does not publish numerically (slot duration, EMA alpha,
 * MAC retry cap) are exposed as tunable parameters here and must be
 * reported explicitly as assumptions in any resulting publication.
 */
#ifndef LEO_WSN_ROUTING_APP_H
#define LEO_WSN_ROUTING_APP_H

#include "atpc.h"
#include "neighbor-table.h"
#include "route-metrics.h"
#include "wsn-net-device.h"

#include <ns3/application.h>
#include <ns3/event-id.h>
#include <ns3/header.h>

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ns3
{
namespace leo
{

/// Application-layer frame types (all carried as a small custom header).
enum class FrameType : uint8_t
{
    NET_SCAN = 0,
    CONNECT = 1,
    PATH_DISCOVERY = 2,
    PATH_DISCOVERY_REPLY = 3,
    PING = 4,
    PING_REPLY = 5,
    ACK = 6 //!< not in the paper's frame-type taxonomy of Section 4; added
            //!< here purely as the simulation's ARQ mechanism so that a
            //!< genuine, observable retransmission count exists to feed
            //!< R_RX/R_TX (Eq. 16). Its energy cost is tracked separately
            //!< under FrameType::ACK and should be reported as such.
};

/// Custom application-level header carrying routing/metric fields.
class WsnHeader : public Header
{
  public:
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;

    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
    uint32_t GetSerializedSize() const override;
    void Print(std::ostream& os) const override;

    FrameType type{FrameType::PING};
    uint32_t originatorId{0}; //!< node id of the flood originator / ping source
    uint32_t targetId{0};     //!< node id of the intended destination
    uint32_t floodId{0};      //!< monotonically increasing per originator
    uint32_t prevHopId{0};    //!< immediate previous hop (for reverse-path setup)
    double accumulatedMetric{0.0}; //!< route metric accumulated so far, Eq. (11)
    uint32_t hopCount{0};
    uint8_t retransmissionCount{0}; //!< MAC-equivalent retry number of this attempt
    uint8_t isRetransmission{0};
    uint32_t seq{0}; //!< ping sequence number
    /// The FrameType of the underlying DATA transaction this header
    /// belongs to. For DATA frames this always equals `type` itself; for
    /// an ACK frame it is set to the type of the frame being acknowledged
    /// (NOT FrameType::ACK). AckKey()/ForwardKey() match on this field
    /// rather than on `type` directly, because otherwise a DATA frame's
    /// pending-transaction key (computed with type=PING, say) would never
    /// match the key computed for its own ACK (whose `type` is
    /// FrameType::ACK) -- a bug that made every ACK silently fail to
    /// match, forcing every unicast frame through its full retry budget
    /// regardless of actual delivery success, and corrupting every
    /// ARQ-derived statistic (R_TX EMA, Eq. 1's delivery-ratio estimate).
    uint8_t origType{0};

    /// Eq. (7): P_TX,adj as computed by the RECEIVER of a WITH_FEEDBACK
    /// data frame, carried back in the ACK header (Fig. 3a) so the
    /// ORIGINAL SENDER can run Algorithm 2 upon receiving that ACK. Only
    /// meaningful when `type == FrameType::ACK`.
    double pTxAdjDb{0.0};

    /// Eq. (9): P_th as computed by the SENDER before transmitting under
    /// ATPC WITHOUT_FEEDBACK (Fig. 3c), carried in the DATA frame itself
    /// so the RECEIVER can run Algorithm 2 upon receiving that DATA frame
    /// and reply with the ACK power computed via Eq. (10).
    double pThDbm{0.0};

    /// The node id this UNICAST frame is physically addressed to for
    /// THIS hop (as opposed to `targetId`, which is the final,
    /// possibly-multi-hop destination). Only meaningful for
    /// PING/PING_REPLY/PATH_DISCOVERY_REPLY/ACK -- broadcast types
    /// (NET_SCAN/CONNECT/PATH_DISCOVERY) are meant for every listener and
    /// ignore this field. WsnChannel delivers every transmission to every
    /// device within probabilistic range regardless of intent (see
    /// wsn-channel.h); without this field, a node that merely overhears a
    /// unicast frame addressed to someone else has no way to know it
    /// wasn't the intended recipient, and previously charged full receive
    /// energy (and, for some frame types, even processed/ACKed it) for
    /// every such overheard transmission -- inflating aggregate energy
    /// figures severalfold in well-connected topologies where many nodes
    /// overhear the same small, frequently-sent ACK frames.
    uint32_t intendedNextHopId{0};
};

struct RouteEntry
{
    bool valid{false};
    uint32_t nextHopId{0};
    double metric{0.0};
    uint32_t hopCount{0};
};

/**
 * \brief Aggregated statistics exposed for post-processing (Figs. 5-10).
 */
struct NodeStats
{
    double energyConsumedMWs{0.0};
    uint32_t pingTimeouts{0}; //!< pings that were actually SENT but never got a reply in time
    uint32_t pingSent{0};
    /// Attempts aborted BEFORE transmission because no route existed yet
    /// (discovery in flight or failed). Kept separate from pingTimeouts:
    /// the paper's own framing ("if any packets are lost, a path
    /// discovery process is triggered") implies pingTimeouts should count
    /// packets that were actually lost in flight, not attempts that never
    /// left the node. Report `pingTimeouts + pingNoRoute` if you want the
    /// paper-equivalent total failure count for a Fig. 6-style plot.
    uint32_t pingNoRoute{0};
    // Energy broken down by frame type, mirroring Fig. 8/9's stacked bars.
    std::map<FrameType, double> energyByType;
};

class WsnRoutingApp : public Application
{
  public:
    static TypeId GetTypeId();
    WsnRoutingApp();

    void Configure(Ptr<WsnNetDevice> device,
                    uint32_t nodeId,
                    bool isGateway,
                    MetricType metricType,
                    const RadioParameters& radio,
                    AtpcMode atpcMode);

    /// Gateway-only: start pinging every known node, 10 pings each, 1 s apart.
    void StartPingCampaign(const std::vector<uint32_t>& targets);

    /// Trigger a flooding-based route discovery towards \p targetId
    /// (Section 3.3). Exposed publicly so a simulation driver can kick off
    /// the "connection phase" (Section 4: "each node ... performed path
    /// discovery one time") without needing an application-level trigger
    /// packet.
    void StartPathDiscovery(uint32_t targetId);

    const NodeStats& GetStats() const
    {
        return m_stats;
    }

    /// Reset MAC-attempt cap and per-attempt slot durations (documented assumptions).
    void SetTiming(double txSlotDurationS, double rxSlotDurationS, uint8_t macMaxRetries);

    /// ACK wait timeout used by the stop-and-wait ARQ reconstruction.
    /// This timing is not published by the source paper and must be
    /// recorded/swept as an experiment parameter.
    void SetAckTimeoutS(double seconds);

    /// Sets the bitrate used to convert frame size into airtime for energy
    /// accounting (Section 3: "Two transmission rates are supported:
    /// 1 Mbit/s and 2 Mbit/s"), and a fixed radio/MAC turnaround overhead
    /// added to every transmission regardless of size.
    void SetPhyTiming(double bitrateBps, double radioOverheadS);

    /// Sets the ping payload size in bytes (Section 4: "The ping packet
    /// data part contains 100 bytes").
    void SetPingPayloadBytes(uint32_t bytes);

    /// Sensitivity-analysis hooks for two documented-but-unpublished
    /// constants (see README's "Assumptions and limitations"): the EMA
    /// smoothing factor for R_RX/R_TX/Eq.(1)'s p_l (default
    /// kDefaultEmaAlpha=0.2), and the destination-side candidate-collection
    /// window (default m_discoveryWindowS=0.15s). Exposed so a sweep can
    /// vary them without recompiling for every value.
    void SetEmaAlpha(double alpha);
    void SetDiscoveryWindowS(double seconds);

    /// Liveness timeout for an outstanding path-discovery transaction.
    /// The source paper does not publish this value; it is a reconstruction
    /// safety parameter and must be frozen/swept in the experiment manifest.
    void SetDiscoveryTimeoutS(double seconds);

    /// Connection-phase entry point (Section 4: "each node ... performed
    /// path discovery one time" during the connection phase, itself
    /// preceded by scanning/joining). Broadcasts one NET_SCAN then one
    /// CONNECT announcement (a joining node has no route yet, so both are
    /// broadcasts rather than a unicast that would assume single-hop
    /// reachability), then triggers StartPathDiscovery(gatewayId) --
    /// giving all frame types (NET_SCAN/CONNECT/PATH_DISCOVERY/PING)
    /// genuine, separately-costed traffic so `energyByType` can support a
    /// Fig. 8-style breakdown.
    void StartConnectionPhase(uint32_t gatewayId);

  protected:
    void StartApplication() override;
    void StopApplication() override;
    void DoDispose() override;

  private:
    friend class WsnRoutingAppTestPeer; // regression-test access only; no runtime behavior

    void OnReceive(Ptr<Packet> packet, Mac48Address from, WsnLinkInfoTag tag);
    void OnReceiveFailed(Ptr<Packet> packet, Mac48Address from, WsnLinkInfoTag tag);

    /// Single-shot, unacknowledged broadcast (used for PATH_DISCOVERY,
    /// matching the paper: flooding needs no per-hop ACK, redundancy
    /// carries the reliability).
    void BroadcastFrame(WsnHeader hdr, uint32_t payloadBytes = 0);

    /// Reliable unicast with a lightweight stop-and-wait ARQ (ACK + timeout
    /// + retry, capped at m_macMaxRetries). On final outcome (success or
    /// retry exhaustion) updates the Neighbor Table's R_TX and reports the
    /// observed retransmission count, which is what Eq. (16)/(17) need.
    void SendUnicastReliable(WsnHeader hdr, uint32_t nextHopId, uint8_t attempt = 0, uint32_t payloadBytes = 0);
    void OnAckTimeout(std::string key);
    void HandleAck(const WsnHeader& hdr, uint32_t fromId);

    struct PendingUnicast
    {
        WsnHeader hdr;
        uint32_t nextHopId{0};
        uint8_t attempt{0};
        uint32_t payloadBytes{0};
        EventId timeoutEvent;
    };
    std::string AckKey(const WsnHeader& hdr) const;
    std::map<std::string, PendingUnicast> m_pendingAcks;
    double m_ackTimeoutS{0.05}; //!< documented assumption, see class comment

    /// Guards against a retransmission storm: SendUnicastReliable retries
    /// on ACK loss, but the DATA frame may have arrived fine each time
    /// (only the ACK was lost). Without this guard, every such duplicate
    /// reception of PATH_DISCOVERY_REPLY/PING_REPLY would re-trigger a
    /// fresh downstream forward, and because forwarding is itself an
    /// ACK'd unicast subject to the same failure mode, the duplication
    /// compounds multiplicatively hop-by-hop (observed in practice: >4^9
    /// frames within ~1s of simulated time). Once a given
    /// (type,originator,target,floodId) has been forwarded once by this
    /// node, later duplicate receptions still get ACKed (so the upstream
    /// sender stops retrying) but are NOT forwarded again.
    std::set<std::string> m_alreadyForwarded;
    std::string ForwardKey(const WsnHeader& hdr) const;

    void HandlePathDiscovery(WsnHeader hdr, uint32_t fromId, const WsnLinkInfoTag& tag);
    void HandlePathDiscoveryReply(WsnHeader hdr, uint32_t fromId, const WsnLinkInfoTag& tag);
    /// Fires when a destination's candidate-collection window closes;
    /// sends the reply via whichever candidate route had the best
    /// (lowest) accumulated metric. See kDiscoveryWindowS.
    void ReplyBestCandidate(uint32_t originatorId, uint32_t floodId);
    void OnDiscoveryTimeout(uint32_t targetId, uint32_t floodId);
    void HandlePing(WsnHeader hdr, uint32_t fromId, const WsnLinkInfoTag& tag);
    void HandlePingReply(WsnHeader hdr, uint32_t fromId, const WsnLinkInfoTag& tag);

    void SendNextPing();
    void OnPingTimeout(uint32_t targetId, uint32_t seq);

    LinkMetricResult LinkMetricTo(uint32_t neighborId) const;
    /// \param frameBytes serialized size of the frame (header + payload),
    /// used to compute airtime when SetPhyTiming has been called; ignored
    /// (falls back to the fixed slot-duration constants) otherwise.
    void ChargeEnergy(FrameType type, double txPowerDbm, bool isTx, uint32_t frameBytes = 0);
    void ChargeRxListeningEnergy(FrameType type, double durationS);

    /// Hard cap on hop count for both PATH_DISCOVERY flood relay and
    /// PATH_DISCOVERY_REPLY forwarding. This is a standard TTL-style
    /// safeguard: the paper's asynchronous "relay again if a better metric
    /// arrives later" behaviour for LEO (Section 4) can otherwise let two
    /// or more nodes update their "next hop toward X" entries out of sync
    /// with each other and form a transient routing loop, which without a
    /// hop limit would forward (and ACK-retry) a frame forever and hang
    /// the simulation. Real deployments need an equivalent bound too.
    static constexpr uint32_t kMaxHopCount = 64;

    /// Cap on how many times a single node may re-relay the *same* flood
    /// (originatorId, floodId) even if later copies keep improving the
    /// LEO metric, bounding the number of rebroadcast events in a densely
    /// connected topology.
    static constexpr uint8_t kMaxRelaysPerFlood = 3;
    std::map<uint32_t, std::map<uint32_t, uint8_t>> m_floodRelayCount; //!< originatorId -> floodId -> count

    /// Destination-side candidate collection: rather than replying to
    /// whichever discovery copy arrives *first* (which, absent modeled
    /// MAC contention/backoff timing, reduces to "shortest/most-direct
    /// path" for every metric and makes Hop-count and ZigBee-LQI
    /// indistinguishable), the destination waits a short window and
    /// replies via whichever candidate had the lowest accumulated metric,
    /// for ALL metric types. This matches Section 2's general requirement
    /// that "the route with the best weight must have the best
    /// performance", and is a documented assumption -- the paper does not
    /// specify an exact collection-window duration.
    struct DiscoveryCandidate
    {
        bool hasCandidate{false};
        double bestMetric{0.0};
        uint32_t bestNextHop{0};
        bool windowScheduled{false};
    };
    std::map<uint32_t, std::map<uint32_t, DiscoveryCandidate>> m_discoveryCandidates;
    double m_discoveryWindowS{0.15}; //!< documented assumption, tunable
    double m_emaAlpha{kDefaultEmaAlpha}; //!< documented assumption, tunable via SetEmaAlpha

    Ptr<WsnNetDevice> m_device;
    uint32_t m_nodeId{0};
    bool m_isGateway{false};
    MetricType m_metricType{MetricType::LEO};
    RadioParameters m_radio;
    AtpcMode m_atpcMode{AtpcMode::WITH_FEEDBACK};
    std::unique_ptr<IRouteMetric> m_metric;

    NeighborTable m_neighborTable;
    std::map<uint32_t, RouteEntry> m_routingTable;
    std::map<uint32_t, Mac48Address> m_addressBook; // nodeId -> MAC, learned opportunistically
    std::map<uint32_t, std::map<uint32_t, double>> m_bestFloodMetricSeen; // originator -> floodId -> best metric relayed so far

    uint32_t m_nextFloodId{1};
    std::map<uint32_t, uint32_t> m_pendingDiscoveryFloodId; // targetId -> floodId in flight
    std::map<uint32_t, EventId> m_discoveryTimeoutEvents;    // targetId -> timeout for current flood
    double m_discoveryTimeoutS{1.0}; //!< reconstruction liveness guard; source value unpublished

    std::vector<uint32_t> m_pingTargets;
    std::size_t m_pingTargetIdx{0};
    uint32_t m_pingSeq{0};
    static constexpr uint32_t kPingsPerNode = 10;
    uint32_t m_pingsSentToCurrentTarget{0};
    EventId m_pingTimer;
    std::map<std::pair<uint32_t, uint32_t>, EventId> m_pingTimeoutEvents; // (targetId, seq) -> timeout

    NodeStats m_stats;

    /// Hard global safety valve: aborts with a diagnostic message instead
    /// of hanging if the total number of transmitted frames across ALL
    /// nodes explodes beyond what any of these small topologies should
    /// plausibly need. This guarantees the simulation can never hang
    /// silently regardless of the root cause; a genuine stall shows up as
    /// a clear NS_FATAL_ERROR instead of a killed/empty run.
    static uint64_t s_globalFrameCounter;
    static constexpr uint64_t kGlobalFrameLimit = 5000000;
    // A GENUINE runaway loop (the two bug classes fixed earlier in this
    // module's history) exhausts this limit almost instantly -- within a
    // small fraction of a simulated second, because a zero-time broadcast
    // cascade or an unbounded ACK-retry storm both compound many, many
    // frames before simulated time advances at all. By contrast, heavy
    // but LEGITIMATE traffic (e.g. a high ping-timeout rate repeatedly
    // triggering full network-wide path-discovery floods, which is
    // exactly the overhead the paper itself identifies as dominant, see
    // Section 4/5) accumulates gradually over the whole run. If you hit
    // this limit, check the last few "heartbeat t=...s" lines: a value
    // near 0s indicates a real bug reintroduced somewhere; a value that
    // grew steadily close to your configured stopTime most likely just
    // means this ceiling needs raising further for your scenario size, or
    // that your link budget/timeout rate needs tuning to reduce
    // discovery-storm overhead (see README's "Assumptions and
    // limitations" -- item 4b).
    static void NoteFrameSent();
    static void ResetGlobalFrameCounter();

    double m_txSlotDurationS{1.0e-3};
    double m_rxSlotDurationS{1.0e-3};
    uint8_t m_macMaxRetries{4};
    static constexpr double kPingTimeoutS = 1.0; // must complete within the 1 s ping interval

    /// Bitrate used to convert a frame's serialized size into airtime
    /// (Section 3: nRF52840 supports 1 or 2 Mbit/s). Defaults to 1 Mbit/s.
    /// When set (via SetPhyTiming), this REPLACES the fixed
    /// m_txSlotDurationS/m_rxSlotDurationS constants above for energy
    /// accounting, since a size-independent slot duration cannot reflect
    /// the paper's own methodology of a 100-byte ping payload (Section 4).
    double m_bitrateBps{1.0e6};
    double m_radioOverheadS{0.0}; //!< fixed MAC/PHY turnaround added to every frame
    bool m_usePacketSizeTiming{false}; //!< becomes true once SetPhyTiming is called
    uint32_t m_pingPayloadBytes{0}; //!< set via SetPingPayloadBytes; paper uses 100
};

} // namespace leo
} // namespace ns3

#endif // LEO_WSN_ROUTING_APP_H
