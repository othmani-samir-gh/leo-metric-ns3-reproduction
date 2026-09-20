#include "wsn-routing-app.h"

#include "nrf52840-current-table.h"
#include <ns3/log.h>
#include <ns3/simulator.h>
#include <ns3/uinteger.h>

#include <algorithm>
#include <cmath>
#include <sstream>

namespace ns3
{
namespace leo
{

NS_LOG_COMPONENT_DEFINE("WsnRoutingApp");

// ---------------------------------------------------------------------
// WsnHeader
// ---------------------------------------------------------------------
TypeId
WsnHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::leo::WsnHeader")
                             .SetParent<Header>()
                             .SetGroupName("LeoWsn")
                             .AddConstructor<WsnHeader>();
    return tid;
}

TypeId
WsnHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

void
WsnHeader::Serialize(Buffer::Iterator start) const
{
    start.WriteU8(static_cast<uint8_t>(type));
    start.WriteHtonU32(originatorId);
    start.WriteHtonU32(targetId);
    start.WriteHtonU32(floodId);
    start.WriteHtonU32(prevHopId);
    // doubles serialized as raw bytes (host order sufficient for a
    // single-process ns-3 simulation)
    double m = accumulatedMetric;
    start.Write(reinterpret_cast<const uint8_t*>(&m), sizeof(double));
    start.WriteHtonU32(hopCount);
    start.WriteU8(retransmissionCount);
    start.WriteU8(isRetransmission);
    start.WriteHtonU32(seq);
    start.WriteU8(origType);
    double a = pTxAdjDb;
    start.Write(reinterpret_cast<const uint8_t*>(&a), sizeof(double));
    double b = pThDbm;
    start.Write(reinterpret_cast<const uint8_t*>(&b), sizeof(double));
    start.WriteHtonU32(intendedNextHopId);
    start.WriteHtonU64(routeFingerprint);
    start.WriteHtonU32(routeHopCount);
}

uint32_t
WsnHeader::Deserialize(Buffer::Iterator start)
{
    type = static_cast<FrameType>(start.ReadU8());
    originatorId = start.ReadNtohU32();
    targetId = start.ReadNtohU32();
    floodId = start.ReadNtohU32();
    prevHopId = start.ReadNtohU32();
    double m;
    start.Read(reinterpret_cast<uint8_t*>(&m), sizeof(double));
    accumulatedMetric = m;
    hopCount = start.ReadNtohU32();
    retransmissionCount = start.ReadU8();
    isRetransmission = start.ReadU8();
    seq = start.ReadNtohU32();
    origType = start.ReadU8();
    double a;
    start.Read(reinterpret_cast<uint8_t*>(&a), sizeof(double));
    pTxAdjDb = a;
    double b;
    start.Read(reinterpret_cast<uint8_t*>(&b), sizeof(double));
    pThDbm = b;
    intendedNextHopId = start.ReadNtohU32();
    routeFingerprint = start.ReadNtohU64();
    routeHopCount = start.ReadNtohU32();
    return GetSerializedSize();
}

uint32_t
WsnHeader::GetSerializedSize() const
{
    return 1 + 4 + 4 + 4 + 4 + sizeof(double) + 4 + 1 + 1 + 4 + 1 + sizeof(double) + sizeof(double) + 4 + 8 + 4;
}

void
WsnHeader::Print(std::ostream& os) const
{
    os << "type=" << static_cast<int>(type) << " orig=" << originatorId
       << " target=" << targetId << " flood=" << floodId << " metric=" << accumulatedMetric
       << " hops=" << hopCount;
}

// ---------------------------------------------------------------------
// WsnRoutingApp
// ---------------------------------------------------------------------
TypeId
WsnRoutingApp::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::leo::WsnRoutingApp").SetParent<Application>().SetGroupName("LeoWsn").AddConstructor<WsnRoutingApp>();
    return tid;
}

uint64_t WsnRoutingApp::s_globalFrameCounter = 0;

uint64_t
MixRouteFingerprint(uint64_t hash, uint32_t nodeId)
{
    static constexpr uint64_t kFnvPrime = 1099511628211ULL;
    for (int shift : {24, 16, 8, 0})
    {
        hash ^= static_cast<uint8_t>((nodeId >> shift) & 0xffu);
        hash *= kFnvPrime;
    }
    return hash;
}

void
WsnRoutingApp::ResetGlobalFrameCounter()
{
    s_globalFrameCounter = 0;
}

void
WsnRoutingApp::NoteFrameSent()
{
    ++s_globalFrameCounter;
    if (s_globalFrameCounter > kGlobalFrameLimit)
    {
        NS_FATAL_ERROR("WsnRoutingApp: global frame counter exceeded "
                        << kGlobalFrameLimit
                        << " -- almost certainly a runaway flood/relay/routing"
                           " loop rather than legitimate traffic for a small"
                           " topology. Aborting instead of hanging; check"
                           " kMaxHopCount/m_maxRelaysPerFlood in"
                           " wsn-routing-app.h and reduce nNodes/spacing/"
                           " envFactor to sanity-check connectivity first.");
    }
}

WsnRoutingApp::WsnRoutingApp()
{
}

void
WsnRoutingApp::Configure(Ptr<WsnNetDevice> device,
                          uint32_t nodeId,
                          bool isGateway,
                          MetricType metricType,
                          const RadioParameters& radio,
                          AtpcMode atpcMode)
{
    m_device = device;
    m_nodeId = nodeId;
    m_isGateway = isGateway;
    m_metricType = metricType;
    m_radio = radio;
    m_atpcMode = atpcMode;
    m_device->SetMaxTxPowerDbm(radio.pTxMaxDbm);
    m_device->SetTxPowerDbm(radio.pTxMaxDbm);

    switch (metricType)
    {
    case MetricType::HOP_COUNT:
        m_metric = std::make_unique<HopCountMetric>();
        break;
    case MetricType::ZIGBEE_LQI:
        m_metric = std::make_unique<ZigbeeLqiMetric>(0.95); // placeholder p_l, refined per-link below
        break;
    case MetricType::ZIGBEE_LQI_LITERAL:
        m_metric = std::make_unique<ZigbeeLqiLiteralMetric>(); // Eq. (2)-(3) verbatim, see class doc comment
        break;
    case MetricType::LEO:
        m_metric = std::make_unique<LeoMetric>();
        break;
    }

    m_device->SetReceiveCallback(MakeCallback(&WsnRoutingApp::OnReceive, this));
    m_device->SetReceiveFailureCallback(MakeCallback(&WsnRoutingApp::OnReceiveFailed, this));

    // Simulator::Destroy() is the authoritative end-of-run boundary in
    // scratch/leo-topologies.cc. Reset the shared safety counter there so
    // independent repetitions cannot inherit each other's frame budget.
    Simulator::ScheduleDestroy(&WsnRoutingApp::ResetGlobalFrameCounter);
}

void
WsnRoutingApp::SetTiming(double txSlotDurationS, double rxSlotDurationS, uint8_t macMaxRetries)
{
    NS_ABORT_MSG_IF(!std::isfinite(txSlotDurationS) || txSlotDurationS <= 0.0,
                    "TX slot duration must be finite and > 0, got " << txSlotDurationS);
    NS_ABORT_MSG_IF(!std::isfinite(rxSlotDurationS) || rxSlotDurationS <= 0.0,
                    "RX slot duration must be finite and > 0, got " << rxSlotDurationS);
    NS_ABORT_MSG_IF(macMaxRetries > kMaxRetransmissionField,
                    "MAC retry cap must fit the protocol retransmission field [0,"
                        << static_cast<uint32_t>(kMaxRetransmissionField)
                        << "], got " << static_cast<uint32_t>(macMaxRetries));
    m_txSlotDurationS = txSlotDurationS;
    m_rxSlotDurationS = rxSlotDurationS;
    m_macMaxRetries = macMaxRetries;
}

void
WsnRoutingApp::SetAckTimeoutS(double seconds)
{
    NS_ABORT_MSG_IF(!std::isfinite(seconds) || seconds <= 0.0,
                    "ACK timeout must be finite and > 0, got " << seconds);
    m_ackTimeoutS = seconds;
}

void
WsnRoutingApp::SetPhyTiming(double bitrateBps, double radioOverheadS)
{
    NS_ABORT_MSG_IF(!std::isfinite(bitrateBps) || bitrateBps <= 0.0,
                    "PHY bitrate must be finite and > 0, got " << bitrateBps);
    NS_ABORT_MSG_IF(!std::isfinite(radioOverheadS) || radioOverheadS < 0.0,
                    "radio overhead must be finite and >= 0, got " << radioOverheadS);
    m_bitrateBps = bitrateBps;
    m_radioOverheadS = radioOverheadS;
    m_usePacketSizeTiming = true;
}

void
WsnRoutingApp::SetPingPayloadBytes(uint32_t bytes)
{
    m_pingPayloadBytes = bytes;
}

void
WsnRoutingApp::SetEmaAlpha(double alpha)
{
    NS_ABORT_MSG_IF(!std::isfinite(alpha) || alpha < 0.0 || alpha > 1.0,
                    "EMA alpha must be finite and in [0,1], got " << alpha);
    m_emaAlpha = alpha;
}

void
WsnRoutingApp::SetDiscoveryWindowS(double seconds)
{
    NS_ABORT_MSG_IF(!std::isfinite(seconds) || seconds < 0.0,
                    "discovery window must be finite and >= 0, got " << seconds);
    m_discoveryWindowS = seconds;
}

void
WsnRoutingApp::SetDiscoveryTimeoutS(double seconds)
{
    NS_ABORT_MSG_IF(!std::isfinite(seconds) || seconds <= 0.0,
                    "discovery timeout must be finite and > 0, got " << seconds);
    m_discoveryTimeoutS = seconds;
}

void
WsnRoutingApp::SetMaxRelaysPerFlood(uint8_t cap)
{
    NS_ABORT_MSG_IF(cap == 0, "max relays per flood must be >= 1");
    m_maxRelaysPerFlood = cap;
}

void
WsnRoutingApp::StartApplication()
{
}

void
WsnRoutingApp::StopApplication()
{
    Simulator::Cancel(m_pingTimer);
    for (auto& kv : m_pingTimeoutEvents)
    {
        Simulator::Cancel(kv.second);
    }
    m_pingTimeoutEvents.clear();
    for (auto& kv : m_discoveryTimeoutEvents)
    {
        Simulator::Cancel(kv.second);
    }
    m_discoveryTimeoutEvents.clear();
    for (auto& kv : m_pendingAcks)
    {
        Simulator::Cancel(kv.second.timeoutEvent);
    }
}

void
WsnRoutingApp::DoDispose()
{
    StopApplication();
    m_pendingAcks.clear();
    m_pendingDiscoveryFloodId.clear();
    m_discoveryCandidates.clear();
    m_bestFloodMetricSeen.clear();
    m_floodRelayCount.clear();
    m_alreadyForwarded.clear();

    if (m_device)
    {
        m_device->SetReceiveCallback(WsnReceiveCallback());
        m_device->SetReceiveFailureCallback(WsnReceiveCallback());
        m_device = nullptr;
    }
    m_metric.reset();
    Application::DoDispose();
}

// -----------------------------------------------------------------
// Energy accounting
// -----------------------------------------------------------------
void
WsnRoutingApp::ChargeEnergy(FrameType type, double txPowerDbm, bool isTx, uint32_t frameBytes)
{
    double durationS;
    if (m_usePacketSizeTiming && frameBytes > 0)
    {
        // Airtime = bits / bitrate + fixed radio/MAC turnaround (P1-3: ties
        // duration to actual frame size instead of a size-independent
        // constant, so the paper's 100-byte ping payload -- Section 4 --
        // genuinely affects the energy total rather than being cosmetic).
        durationS = (8.0 * frameBytes) / m_bitrateBps + m_radioOverheadS;
    }
    else
    {
        // Fallback: fixed slot duration (documented assumption, see
        // README's "Assumptions and limitations").
        durationS = isTx ? m_txSlotDurationS : m_rxSlotDurationS;
    }
    // TX cost uses the actual radiated power (Eq. 14/15 units: mW); RX cost
    // uses the constant reception penalty P_R (Eq. 14) rather than TX
    // power, since P_R models the receiver chain's own consumption.
    double powerMw;
    if (m_radio.useNrf52840Energy)
    {
        // Discrete-level, datasheet-anchored model (see
        // nrf52840-current-table.h). txPowerDbm should already have been
        // snapped by the caller (SendUnicastReliable/BroadcastFrame) so
        // the radiated power and the energy accounting agree.
        powerMw = isTx ? Nrf52840TxPowerMw(txPowerDbm, m_radio.hfxoStandbyCurrentMa)
                       : Nrf52840RxPowerMw(m_bitrateBps, m_radio.hfxoStandbyCurrentMa);
    }
    else
    {
        powerMw = isTx ? DbmToMw(txPowerDbm) : m_radio.rxPowerPenaltyMw;
    }
    double energyMWs = powerMw * durationS; // mW * s = mWs directly; no extra factor needed
                                              // (a previous version erroneously multiplied by
                                              // 1000, inflating every absolute energy figure by
                                              // 1000x -- a constant factor that did not affect
                                              // relative LEO/Hop-count/LQI comparisons but
                                              // invalidated any comparison to the paper's
                                              // absolute mWs figures).
    // Defensive guard: a negative, NaN, or infinite per-frame energy
    // value can only come from a genuine bug upstream (e.g. a corrupted
    // duration or power figure) -- fail loudly here rather than silently
    // accumulating a nonsensical value into the run's totals, which would
    // be far harder to notice after the fact than an aborted run.
    NS_ABORT_MSG_IF(!std::isfinite(energyMWs) || energyMWs < 0.0,
                     "ChargeEnergy: non-physical per-frame energy computed ("
                         << energyMWs << " mWs, powerMw=" << powerMw << ", durationS=" << durationS << ")");
    m_stats.energyConsumedMWs += energyMWs;
    m_stats.energyByType[type] += energyMWs;
    m_device->AddEnergyMWs(energyMWs);
}

void
WsnRoutingApp::ChargeRxListeningEnergy(FrameType type, double durationS)
{
    NS_ABORT_MSG_IF(!std::isfinite(durationS) || durationS < 0.0,
                    "ChargeRxListeningEnergy: invalid duration " << durationS);

    double powerMw = m_radio.useNrf52840Energy
                         ? Nrf52840RxPowerMw(m_bitrateBps, m_radio.hfxoStandbyCurrentMa)
                         : m_radio.rxPowerPenaltyMw;
    double energyMWs = powerMw * durationS;
    NS_ABORT_MSG_IF(!std::isfinite(energyMWs) || energyMWs < 0.0,
                    "ChargeRxListeningEnergy: non-physical energy " << energyMWs);

    m_stats.energyConsumedMWs += energyMWs;
    m_stats.energyByType[type] += energyMWs;
    m_device->AddEnergyMWs(energyMWs);
}

// -----------------------------------------------------------------
// Link metric helper
// -----------------------------------------------------------------
LinkMetricResult
WsnRoutingApp::LinkMetricTo(uint32_t neighborId) const
{
    // NeighborTable is keyed by Ipv4Address in the generic module (see
    // neighbor-table.h); here we key by a synthetic address derived from
    // the node id to avoid depending on ns-3's IP stack, which this
    // application-layer protocol does not use (see wsn-net-device.h).
    Ipv4Address key(neighborId);
    if (!m_neighborTable.Has(key))
    {
        // Unknown neighbor: paper's fallback for hop-count/LQI defaults
        // (C{l}=7 for unknown p_l, Eq. 1) and for LEO (R = R_max, Eq. 16).
        NeighborEntry blank;
        return m_metric->ComputeLinkMetric(blank, m_radio);
    }
    const NeighborEntry& e = m_neighborTable.GetTable().at(key);
    return m_metric->ComputeLinkMetric(e, m_radio);
}

// -----------------------------------------------------------------
// Transmission primitives
// -----------------------------------------------------------------
void
WsnRoutingApp::BroadcastFrame(WsnHeader hdr, uint32_t payloadBytes)
{
    hdr.origType = static_cast<uint8_t>(hdr.type); // see WsnHeader::origType doc comment
    NoteFrameSent();
    Ptr<Packet> pkt = Create<Packet>(payloadBytes);
    pkt->AddHeader(hdr);
    // Broadcasts always use max power: "transmission power is always set
    // to the maximum possible value when broadcasting" (Section 3.3).
    double txPowerDbm = RealizeNrf52840TxPowerDbm(m_radio.pTxMaxDbm);
    m_device->SetTxPowerDbm(txPowerDbm);
    m_device->Send(pkt);
    ChargeEnergy(hdr.type, txPowerDbm, true, hdr.GetSerializedSize() + payloadBytes);
}

std::string
WsnRoutingApp::AckKey(const WsnHeader& hdr) const
{
    std::ostringstream oss;
    oss << static_cast<int>(hdr.origType) << ':' << hdr.originatorId << ':' << hdr.targetId << ':'
        << hdr.floodId << ':' << hdr.seq;
    return oss.str();
}

std::string
WsnRoutingApp::ForwardKey(const WsnHeader& hdr) const
{
    return AckKey(hdr); // identical field set (type,orig,target,flood,seq) is sufficient
}

void
WsnRoutingApp::SendUnicastReliable(WsnHeader hdr, uint32_t nextHopId, uint8_t attempt, uint32_t payloadBytes)
{
    NoteFrameSent();
    hdr.origType = static_cast<uint8_t>(hdr.type); // see WsnHeader::origType doc comment
    hdr.retransmissionCount = attempt;
    hdr.isRetransmission = attempt > 0 ? 1 : 0;
    hdr.prevHopId = m_nodeId;
    hdr.intendedNextHopId = nextHopId; // see WsnHeader::intendedNextHopId doc comment

    Ipv4Address key(nextHopId);
    double requestedTxPowerDbm = m_radio.pTxMaxDbm;
    if (m_neighborTable.Has(key) && m_neighborTable.Get(key).reqTxPowerKnown)
    {
        requestedTxPowerDbm =
            std::clamp(m_neighborTable.Get(key).reqTxPowerDbm, m_radio.pTxMinDbm, m_radio.pTxMaxDbm);
    }
    // Hardware realization is a radio property, not an energy-accounting
    // option.  Use this exact realized value for channel delivery and
    // subsequent energy accounting.
    double txPowerDbm = RealizeNrf52840TxPowerDbm(requestedTxPowerDbm);

    if (m_atpcMode == AtpcMode::WITHOUT_FEEDBACK)
    {
        // Algorithm 1, without-feedback branch: compute P_th (Eq. 9) and
        // embed it so the receiver can run Algorithm 2 upon reception
        // (Fig. 3c). rxAntennaGainDb is taken as 0 dB throughout this
        // module (see README's antenna-gain note).
        hdr.pThDbm = AlgorithmOnePreTransmission(AtpcMode::WITHOUT_FEEDBACK, 0.0, m_radio, 0.0);
    }

    Ptr<Packet> pkt = Create<Packet>(payloadBytes);
    pkt->AddHeader(hdr);
    m_device->SetTxPowerDbm(txPowerDbm);
    m_device->SendTo(pkt, nextHopId); // point-to-point delivery, evaluated only for nextHopId
    ChargeEnergy(hdr.type, txPowerDbm, true, hdr.GetSerializedSize() + payloadBytes);

    std::string key2 = AckKey(hdr);
    PendingUnicast pu;
    pu.hdr = hdr;
    pu.nextHopId = nextHopId;
    pu.attempt = attempt;
    pu.payloadBytes = payloadBytes;
    pu.timeoutEvent = Simulator::Schedule(Seconds(m_ackTimeoutS), &WsnRoutingApp::OnAckTimeout, this, key2);
    m_pendingAcks[key2] = pu;
}

void
WsnRoutingApp::OnAckTimeout(std::string key)
{
    auto it = m_pendingAcks.find(key);
    if (it == m_pendingAcks.end())
    {
        return; // already ACKed
    }
    PendingUnicast pu = it->second;
    m_pendingAcks.erase(it);

    // The sender kept its receiver on while waiting for an ACK that never
    // arrived.  Successful ACK frame reception is already charged in
    // OnReceive(); this explicitly accounts only the missing-ACK listen
    // window that was previously free.
    ChargeRxListeningEnergy(FrameType::ACK, m_ackTimeoutS);

    Ipv4Address neighKey(pu.nextHopId);
    m_neighborTable.UpdateDeliveryOutcome(neighKey, false, m_emaAlpha); // this attempt failed, Eq. (1)'s p_l input
    if (pu.attempt + 1 > m_macMaxRetries)
    {
        // Final per-hop ARQ exhaustion is concrete evidence that this
        // next hop is currently unusable. Invalidate every route that
        // depends on it; otherwise later packets keep selecting the same
        // dead next hop until an unrelated end-to-end timeout happens.
        m_neighborTable.UpdateTxRetransmissions(neighKey, m_macMaxRetries, m_emaAlpha);
        for (auto& kv : m_routingTable)
        {
            RouteEntry& route = kv.second;
            if (route.valid && route.nextHopId == pu.nextHopId)
            {
                route.valid = false;
            }
        }
        return;
    }
    SendUnicastReliable(pu.hdr, pu.nextHopId, pu.attempt + 1, pu.payloadBytes);
}

void
WsnRoutingApp::HandleAck(const WsnHeader& hdr, uint32_t fromId)
{
    std::string key = AckKey(hdr);
    auto it = m_pendingAcks.find(key);
    if (it == m_pendingAcks.end())
    {
        return;
    }
    Simulator::Cancel(it->second.timeoutEvent);
    uint8_t observedRetx = it->second.attempt;
    m_pendingAcks.erase(it);
    Ipv4Address neighKey(fromId);
    m_neighborTable.UpdateTxRetransmissions(neighKey, observedRetx, m_emaAlpha);
    m_neighborTable.UpdateDeliveryOutcome(neighKey, true, m_emaAlpha); // this attempt succeeded, Eq. (1)'s p_l input

    // Algorithm 2, with-feedback branch (Fig. 3a): this ACK carries the
    // P_TX,adj that the peer computed upon receiving our data frame
    // (Eq. 7); apply it now to update OUR required TX power for that
    // peer (P_TX,R). "wasRetransmitted" is approximated here as "this
    // transmission needed at least one retry before being ACKed" -- the
    // paper's Algorithm 2 pseudocode does not fully specify this trigger
    // in a multi-attempt ARQ context; treating a retried-then-successful
    // exchange as evidence of a weak link (snapping to P_TX,max) is a
    // documented, conservative interpretation.
    if (m_atpcMode == AtpcMode::WITH_FEEDBACK)
    {
        NeighborEntry& e = m_neighborTable.Get(neighKey);
        bool wasRetransmitted = observedRetx > 0;
        double newReq = AlgorithmTwoPostReception(wasRetransmitted,
                                                    AtpcMode::WITH_FEEDBACK,
                                                    e.reqTxPowerDbm,
                                                    e.reqTxPowerKnown,
                                                    hdr.pTxAdjDb,
                                                    0.0,
                                                    e.linkSignalLossDb,
                                                    m_radio);
        e.reqTxPowerDbm = std::clamp(newReq, m_radio.pTxMinDbm, m_radio.pTxMaxDbm);
        e.reqTxPowerKnown = true;
        // This is exactly "P_TX,RN -- the transmission power requested BY
        // the neighbor" (Eq. 12): our own ReqTXP[peer], now confirmed via
        // the peer's ACK feedback, IS what the peer is asking us to use.
        e.pTxRequestedByNeighborDbm = e.reqTxPowerDbm;
        e.pTxRequestedByNeighborKnown = true;
    }
}

// -----------------------------------------------------------------
// Reception dispatch
// -----------------------------------------------------------------
void
WsnRoutingApp::OnReceiveFailed(Ptr<Packet> packet, Mac48Address /*from*/, WsnLinkInfoTag /*tag*/)
{
    uint32_t frameBytes = packet->GetSize();
    WsnHeader hdr;
    packet->RemoveHeader(hdr);

    // For unicast the channel calls this only on the intended receiver.
    // Broadcast frames are listened to by every attached receiver.
    // A failed decode consumes radio RX energy but MUST NOT update
    // neighbor/routing/ATPC state or emit an ACK.
    ChargeEnergy(hdr.type, 0.0, false, frameBytes);
}

void
WsnRoutingApp::OnReceive(Ptr<Packet> packet, Mac48Address from, WsnLinkInfoTag tag)
{
    uint32_t frameBytes = packet->GetSize(); // capture before RemoveHeader consumes the header
    WsnHeader hdr;
    packet->RemoveHeader(hdr);
    uint32_t fromId = hdr.prevHopId;

    // Defense-in-depth guard: as of the WsnChannel::SendUnicast change
    // (point-to-point delivery evaluated only for the intended
    // recipient, see wsn-channel.h), this branch should now be
    // unreachable for genuinely unicast frame types -- the channel
    // itself only ever schedules Receive() on the intended device for
    // those. It is kept as a cheap, harmless safety net (e.g. against a
    // future bug that routes a unicast-typed frame through the broadcast
    // Send() path by mistake) rather than removed. NET_SCAN/CONNECT/
    // PATH_DISCOVERY remain genuinely broadcast and skip this check.
    if (hdr.type != FrameType::NET_SCAN && hdr.type != FrameType::CONNECT &&
        hdr.type != FrameType::PATH_DISCOVERY && hdr.intendedNextHopId != m_nodeId)
    {
        return;
    }

    // Charge the receive-side energy cost (P_R, Eq. 14) for successfully
    // decoding this frame. Previously ChargeEnergy was only ever called
    // with isTx=true, so reception cost was silently omitted from every
    // energy total despite the accounting machinery already supporting it.
    ChargeEnergy(hdr.type, 0.0, false, frameBytes);

    // Opportunistically learn/update the neighbor table for this hop
    // (Eq. 6: LSL = RSSI_m - P_TX - G_RX, with G_RX assumed 0 dB here).
    Ipv4Address key(fromId);
    NeighborEntry& entry = m_neighborTable.Get(key);
    entry.linkSignalLossDb = tag.rssiDbm - tag.txPowerDbm - 0.0;
    entry.lslKnown = true;
    entry.lastRssiDbm = tag.rssiDbm;
    entry.rssiKnown = true;
    entry.lastSnrDb = tag.snrDb;
    entry.snrKnown = true;
    m_neighborTable.UpdateRxRetransmissions(key, hdr.retransmissionCount, m_emaAlpha); // hdr, not tag: see WsnLinkInfoTag doc
    if (!entry.maxTxPowerKnown)
    {
        entry.maxTxPowerDbm = m_radio.pTxMaxDbm; // homogeneous radios assumption
        entry.maxTxPowerKnown = true;
    }

    // Algorithm 1, with-feedback branch (Fig. 3a): "After receiving the
    // frame, node B compares the SNR ... P_TX,adj <- SNR_th - SNR". This
    // is computed HERE at the receiver from the frame just measured, but
    // -- per the paper -- is only *applied* by the ORIGINAL SENDER once
    // it is carried back in the ACK and processed there (see HandleAck's
    // Algorithm 2 call). We only stage the value into the outgoing ACK
    // below; we do NOT update our own neighbor-table entry for `fromId`
    // here (a prior version incorrectly did, applying the adjustment
    // unilaterally at the receiver instead of feeding it back via ACK).
    double pendingTxAdjDb = 0.0;
    if (m_atpcMode == AtpcMode::WITH_FEEDBACK && hdr.type != FrameType::ACK)
    {
        pendingTxAdjDb = ComputeTxPowerAdjustmentDb(tag.snrDb, m_radio);
    }

    // Algorithm 2, without-feedback branch (Fig. 3c): triggered by
    // RECEIVING a DATA frame that itself carries P_th (Eq. 9, computed by
    // its sender via Algorithm 1's else-branch). We update OUR OWN
    // required TX power for `fromId` here, since this is the "adjustment
    // of transmission power after frame reception" the algorithm
    // describes, run by the node that will next need to transmit back.
    if (m_atpcMode == AtpcMode::WITHOUT_FEEDBACK && hdr.type != FrameType::ACK &&
        hdr.type != FrameType::PATH_DISCOVERY && hdr.type != FrameType::NET_SCAN &&
        hdr.type != FrameType::CONNECT)
    {
        bool wasRetransmitted = hdr.isRetransmission != 0;
        double newReq = AlgorithmTwoPostReception(wasRetransmitted,
                                                    AtpcMode::WITHOUT_FEEDBACK,
                                                    entry.reqTxPowerDbm,
                                                    entry.reqTxPowerKnown,
                                                    0.0,
                                                    hdr.pThDbm,
                                                    entry.linkSignalLossDb,
                                                    m_radio);
        entry.reqTxPowerDbm = std::clamp(newReq, m_radio.pTxMinDbm, m_radio.pTxMaxDbm);
        entry.reqTxPowerKnown = true;
        entry.pTxRequestedByNeighborDbm = entry.reqTxPowerDbm;
        entry.pTxRequestedByNeighborKnown = true;
    }

    if (hdr.type == FrameType::ACK)
    {
        HandleAck(hdr, fromId);
        return;
    }

    // All non-ACK frame types other than PATH_DISCOVERY/NET_SCAN/CONNECT
    // expect an ACK back (the latter two are broadcasts -- like
    // PATH_DISCOVERY -- and must not be ACKed by every listener).
    if (hdr.type != FrameType::PATH_DISCOVERY && hdr.type != FrameType::NET_SCAN &&
        hdr.type != FrameType::CONNECT)
    {
        WsnHeader ack;
        ack.type = FrameType::ACK;
        ack.origType = static_cast<uint8_t>(hdr.type); // must match the DATA frame's key, not ACK's own type
        ack.originatorId = hdr.originatorId;
        ack.targetId = hdr.targetId;
        ack.floodId = hdr.floodId;
        ack.seq = hdr.seq;
        ack.prevHopId = m_nodeId;
        ack.intendedNextHopId = fromId; // address the ACK back to whoever sent the DATA frame
        ack.pTxAdjDb = pendingTxAdjDb; // Eq. (7), staged above; 0 if not in WITH_FEEDBACK mode
        Ptr<Packet> ackPkt = Create<Packet>();
        ackPkt->AddHeader(ack);
        double requestedAckPowerDbm =
            m_neighborTable.Has(key) && m_neighborTable.Get(key).reqTxPowerKnown
                ? std::clamp(m_neighborTable.Get(key).reqTxPowerDbm,
                             m_radio.pTxMinDbm,
                             m_radio.pTxMaxDbm)
                : m_radio.pTxMaxDbm;
        double ackPowerDbm = RealizeNrf52840TxPowerDbm(requestedAckPowerDbm);
        m_device->SetTxPowerDbm(ackPowerDbm);
        m_device->SendTo(ackPkt, fromId); // point-to-point: evaluated only for fromId
        ChargeEnergy(FrameType::ACK, ackPowerDbm, true, ack.GetSerializedSize());
    }

    switch (hdr.type)
    {
    case FrameType::PATH_DISCOVERY:
        HandlePathDiscovery(hdr, fromId, tag);
        break;
    case FrameType::PATH_DISCOVERY_REPLY:
        HandlePathDiscoveryReply(hdr, fromId, tag);
        break;
    case FrameType::PING:
        HandlePing(hdr, fromId, tag);
        break;
    case FrameType::PING_REPLY:
        HandlePingReply(hdr, fromId, tag);
        break;
    default:
        break;
    }
}

// -----------------------------------------------------------------
// Path discovery (flooding), Section 3.3
// -----------------------------------------------------------------
void
WsnRoutingApp::StartPathDiscovery(uint32_t targetId)
{
    // A target may only have one locally-current discovery transaction.
    // If a caller explicitly restarts it, cancel the previous liveness
    // timer before replacing the authoritative flood id.
    auto oldTimeout = m_discoveryTimeoutEvents.find(targetId);
    if (oldTimeout != m_discoveryTimeoutEvents.end())
    {
        Simulator::Cancel(oldTimeout->second);
        m_discoveryTimeoutEvents.erase(oldTimeout);
    }

    WsnHeader hdr;
    hdr.type = FrameType::PATH_DISCOVERY;
    hdr.originatorId = m_nodeId;
    hdr.targetId = targetId;
    hdr.floodId = m_nextFloodId++;
    hdr.prevHopId = m_nodeId;
    hdr.accumulatedMetric = 0.0;
    hdr.hopCount = 0;
    m_pendingDiscoveryFloodId[targetId] = hdr.floodId;
    m_bestFloodMetricSeen[m_nodeId][hdr.floodId] = 0.0;
    m_discoveryTimeoutEvents[targetId] =
        Simulator::Schedule(Seconds(m_discoveryTimeoutS),
                            &WsnRoutingApp::OnDiscoveryTimeout,
                            this,
                            targetId,
                            hdr.floodId);
    BroadcastFrame(hdr);
}

void
WsnRoutingApp::OnDiscoveryTimeout(uint32_t targetId, uint32_t floodId)
{
    // Transaction-safe timeout: a timer belonging to an older flood must
    // never clear a newer discovery for the same target.
    auto pending = m_pendingDiscoveryFloodId.find(targetId);
    if (pending == m_pendingDiscoveryFloodId.end() || pending->second != floodId)
    {
        return;
    }

    m_pendingDiscoveryFloodId.erase(pending);
    m_discoveryTimeoutEvents.erase(targetId);
}

void
WsnRoutingApp::HandlePathDiscovery(WsnHeader hdr, uint32_t fromId, const WsnLinkInfoTag& /*tag*/)
{
    LinkMetricResult link = LinkMetricTo(fromId);
    if (!link.linkUsable)
    {
        return; // Eq. (12)-(13): one-way/unusable links cannot enter a route.
    }
    double newAccumulated = hdr.accumulatedMetric + link.value;
    uint32_t newHopCount = hdr.hopCount + 1;

    if (newHopCount > kMaxHopCount)
    {
        return; // TTL exceeded, drop (see kMaxHopCount doc comment)
    }

    if (m_nodeId == hdr.targetId)
    {
        // Destination role: collect candidates from every distinct path
        // that reaches us before the window closes, and reply via the
        // best one -- for ALL metric types, not just LEO (see
        // DiscoveryCandidate doc comment in the header for why).
        DiscoveryCandidate& cand = m_discoveryCandidates[hdr.originatorId][hdr.floodId];
        if (!cand.hasCandidate || newAccumulated < cand.bestMetric)
        {
            cand.hasCandidate = true;
            cand.bestMetric = newAccumulated;
            cand.bestNextHop = fromId;

            RouteEntry& re = m_routingTable[hdr.originatorId];
            re.valid = true;
            re.nextHopId = fromId;
            re.metric = newAccumulated;
            re.hopCount = newHopCount;
        }
        if (!cand.windowScheduled)
        {
            cand.windowScheduled = true;
            Simulator::Schedule(Seconds(m_discoveryWindowS),
                                 &WsnRoutingApp::ReplyBestCandidate,
                                 this,
                                 hdr.originatorId,
                                 hdr.floodId);
        }
        return;
    }

    // Relay role: first-arrival suppression for Hop-count/ZigBee-LQI;
    // bounded re-relay-on-improvement for LEO (Section 4).
    auto& seenForFlood = m_bestFloodMetricSeen[hdr.originatorId];
    auto it = seenForFlood.find(hdr.floodId);
    bool firstTime = (it == seenForFlood.end());
    bool better = firstTime || (m_metricType == MetricType::LEO && newAccumulated < it->second);

    if (!better)
    {
        return; // suppressed duplicate, matches first-seen flood suppression
    }

    auto& relayCount = m_floodRelayCount[hdr.originatorId][hdr.floodId];
    if (!firstTime && relayCount >= m_maxRelaysPerFlood)
    {
        return; // re-relay budget exhausted for this flood, see m_maxRelaysPerFlood
    }
    relayCount++;
    seenForFlood[hdr.floodId] = newAccumulated;

    // Update the reverse route toward the flood originator.
    RouteEntry& re = m_routingTable[hdr.originatorId];
    re.valid = true;
    re.nextHopId = fromId;
    re.metric = newAccumulated;
    re.hopCount = newHopCount;

    // Continue the flood.
    WsnHeader relay = hdr;
    relay.prevHopId = m_nodeId;
    relay.accumulatedMetric = newAccumulated;
    relay.hopCount = newHopCount;
    BroadcastFrame(relay);
}

void
WsnRoutingApp::ReplyBestCandidate(uint32_t originatorId, uint32_t floodId)
{
    auto originIt = m_discoveryCandidates.find(originatorId);
    if (originIt == m_discoveryCandidates.end())
    {
        return;
    }
    auto it = originIt->second.find(floodId);
    if (it == originIt->second.end() || !it->second.hasCandidate)
    {
        return;
    }
    uint32_t bestNextHop = it->second.bestNextHop;
    originIt->second.erase(it);

    WsnHeader reply;
    reply.type = FrameType::PATH_DISCOVERY_REPLY;
    reply.originatorId = originatorId;
    reply.targetId = m_nodeId;
    reply.floodId = floodId;
    reply.accumulatedMetric = 0.0;
    reply.hopCount = 0;
    reply.seq = 0;
    SendUnicastReliable(reply, bestNextHop);
}

void
WsnRoutingApp::HandlePathDiscoveryReply(WsnHeader hdr, uint32_t fromId, const WsnLinkInfoTag& /*tag*/)
{
    // Transaction identity is checked BEFORE any routing-table mutation.
    // At the originator, only the exact currently pending flood may
    // complete discovery. At a relay, require that this node actually
    // relayed the same flood and has not since observed a newer flood from
    // that originator.
    if (m_nodeId == hdr.originatorId)
    {
        auto pending = m_pendingDiscoveryFloodId.find(hdr.targetId);
        if (pending == m_pendingDiscoveryFloodId.end() || pending->second != hdr.floodId)
        {
            return;
        }
    }
    else
    {
        auto seenOrigin = m_bestFloodMetricSeen.find(hdr.originatorId);
        if (seenOrigin == m_bestFloodMetricSeen.end() ||
            seenOrigin->second.find(hdr.floodId) == seenOrigin->second.end() ||
            seenOrigin->second.rbegin()->first != hdr.floodId)
        {
            return;
        }
    }

    LinkMetricResult link = LinkMetricTo(fromId);
    if (!link.linkUsable)
    {
        return; // Eq. (12)-(13): reply path must also remain bidirectional.
    }
    double newAccumulated = hdr.accumulatedMetric + link.value;
    uint32_t newHopCount = hdr.hopCount + 1;

    if (newHopCount > kMaxHopCount)
    {
        return;
    }

    RouteEntry& re = m_routingTable[hdr.targetId];
    re.valid = true;
    re.nextHopId = fromId;
    re.metric = newAccumulated;
    re.hopCount = newHopCount;

    if (m_nodeId == hdr.originatorId)
    {
        auto timeout = m_discoveryTimeoutEvents.find(hdr.targetId);
        if (timeout != m_discoveryTimeoutEvents.end())
        {
            Simulator::Cancel(timeout->second);
            m_discoveryTimeoutEvents.erase(timeout);
        }
        m_pendingDiscoveryFloodId.erase(hdr.targetId);
        return;
    }

    std::string fkey = ForwardKey(hdr);
    if (m_alreadyForwarded.count(fkey))
    {
        // Duplicate delivery of a reply we already forwarded once (almost
        // certainly because our ACK back to the previous hop was lost, not
        // because the data failed) -- the routing-table update above is
        // harmless to repeat, but forwarding again would compound into an
        // exponential retry storm hop-by-hop. Do not re-send.
        return;
    }
    m_alreadyForwarded.insert(fkey);

    // Keep unicasting the reply toward the flood originator via our own
    // (already-known) reverse route.
    auto it = m_routingTable.find(hdr.originatorId);
    if (it == m_routingTable.end() || !it->second.valid)
    {
        return; // should not normally happen: we relayed the forward flood
    }
    if (it->second.nextHopId == fromId)
    {
        // Immediate 2-node ping-pong: our route back to the originator
        // currently points straight back at whoever just sent us this
        // reply, which would bounce it forever. Drop rather than loop;
        // this is a direct symptom of the asynchronous route updates
        // documented at kMaxHopCount.
        return;
    }
    WsnHeader relay = hdr;
    relay.accumulatedMetric = newAccumulated;
    relay.hopCount = newHopCount;
    SendUnicastReliable(relay, it->second.nextHopId);
}

// -----------------------------------------------------------------
// Ping campaign (Section 4 methodology)
// -----------------------------------------------------------------
void
WsnRoutingApp::StartPingCampaign(const std::vector<uint32_t>& targets)
{
    m_pingTargets = targets;
    m_pingTargetIdx = 0;
    m_pingsSentToCurrentTarget = 0;
    Simulator::ScheduleNow(&WsnRoutingApp::SendNextPing, this);
}

void
WsnRoutingApp::StartConnectionPhase(uint32_t gatewayId)
{
    // NET_SCAN: broadcast presence announcement (Section 4's "Net scan"
    // packet type). Kept intentionally minimal (no neighbor-list payload)
    // since the paper does not specify NET_SCAN's exact content, only
    // that it precedes joining.
    WsnHeader scan;
    scan.type = FrameType::NET_SCAN;
    scan.originatorId = m_nodeId;
    scan.targetId = gatewayId;
    BroadcastFrame(scan);

    // CONNECT: broadcast join announcement (Section 4's "Connect" packet
    // type). Broadcast rather than unicast because a joining node has no
    // route yet -- unicasting it to gatewayId would incorrectly assume
    // single-hop reachability.
    WsnHeader conn;
    conn.type = FrameType::CONNECT;
    conn.originatorId = m_nodeId;
    conn.targetId = gatewayId;
    BroadcastFrame(conn);

    // Path discovery (Section 3.3), giving NET_SCAN/CONNECT/PATH_DISCOVERY
    // genuinely distinct, separately-costed traffic so `energyByType` can
    // support a Fig. 8-style breakdown (P1-4 in the review).
    StartPathDiscovery(gatewayId);
}

void
WsnRoutingApp::SendNextPing()
{
    // Skip past any exhausted targets with NO simulated-time cost (this
    // used to burn a full extra 1 s per target transition via a
    // reschedule-and-return pattern, silently shortening the campaign by
    // (targets.size()-1) seconds and cutting off the last few targets
    // before their 10 pings completed -- fixed by advancing in a tight
    // loop here instead).
    while (m_pingTargetIdx < m_pingTargets.size() && m_pingsSentToCurrentTarget >= kPingsPerNode)
    {
        m_pingTargetIdx++;
        m_pingsSentToCurrentTarget = 0;
    }
    if (m_pingTargetIdx >= m_pingTargets.size())
    {
        return; // campaign complete
    }
    uint32_t target = m_pingTargets[m_pingTargetIdx];

    auto rtIt = m_routingTable.find(target);
    bool haveRoute = rtIt != m_routingTable.end() && rtIt->second.valid;
    if (!haveRoute && m_pendingDiscoveryFloodId.find(target) == m_pendingDiscoveryFloodId.end())
    {
        StartPathDiscovery(target);
    }

    if (haveRoute)
    {
        WsnHeader hdr;
        hdr.type = FrameType::PING;
        hdr.originatorId = m_nodeId;
        hdr.targetId = target;
        hdr.seq = m_pingSeq;
        hdr.hopCount = 0;
        hdr.accumulatedMetric = 0.0;
        static constexpr uint64_t kRouteFingerprintOffset = 14695981039346656037ULL;
        hdr.routeFingerprint = MixRouteFingerprint(kRouteFingerprintOffset, m_nodeId);
        hdr.routeHopCount = 0;
        m_stats.pingTrace[{target, m_pingSeq}] = {"pending", 0, 0};
        m_stats.pingSent++;
        SendUnicastReliable(hdr, rtIt->second.nextHopId, 0, m_pingPayloadBytes);
        auto pingKey = std::make_pair(target, m_pingSeq);
        m_pingTimeoutEvents[pingKey] =
            Simulator::Schedule(Seconds(kPingTimeoutS), &WsnRoutingApp::OnPingTimeout, this, target, m_pingSeq);
    }
    else
    {
        // No route yet (discovery in flight or failed): this attempt never
        // left the node, so it is counted separately from pingTimeouts
        // (which now means "sent but no reply arrived in time"). Add both
        // columns together for a Fig. 6-equivalent total failure count.
        m_stats.pingNoRoute++;
        m_stats.pingTrace[{target, m_pingSeq}] = {"no_route", 0, 0};
    }

    m_pingSeq++;
    m_pingsSentToCurrentTarget++;
    m_pingTimer = Simulator::Schedule(Seconds(1.0), &WsnRoutingApp::SendNextPing, this);
}

void
WsnRoutingApp::OnPingTimeout(uint32_t targetId, uint32_t seq)
{
    auto pingKey = std::make_pair(targetId, seq);
    auto timeout = m_pingTimeoutEvents.find(pingKey);
    if (timeout == m_pingTimeoutEvents.end())
    {
        return; // matching reply already completed this exact transaction
    }
    m_pingTimeoutEvents.erase(timeout);

    m_stats.pingTimeouts++;
    m_stats.pingTrace[pingKey] = {"timeout", 0, 0};
    auto it = m_routingTable.find(targetId);
    if (it != m_routingTable.end())
    {
        it->second.valid = false;
    }

    // StartPathDiscovery is restart-safe: it cancels any older discovery
    // timeout and replaces the target's authoritative flood id.
    StartPathDiscovery(targetId);
}

void
WsnRoutingApp::HandlePing(WsnHeader hdr, uint32_t fromId, const WsnLinkInfoTag& /*tag*/)
{
    if (hdr.targetId != m_nodeId)
    {
        // Forward toward the real destination via our own route table,
        // mirroring HandlePingReply's forwarding branch. Earlier this
        // case simply dropped the frame, which meant ANY route requiring
        // more than one hop from the gateway would silently fail every
        // single ping at the first intermediate hop -- a simulation
        // artifact, not a genuine physical-layer failure, and a likely
        // contributor to the unexpectedly high timeout rates seen in
        // multi-hop-heavy scenarios.
        std::string fkey = ForwardKey(hdr);
        if (m_alreadyForwarded.count(fkey))
        {
            return; // duplicate delivery of a ping we already forwarded once
        }
        auto it = m_routingTable.find(hdr.targetId);
        if (it != m_routingTable.end() && it->second.valid && it->second.nextHopId != fromId)
        {
            m_alreadyForwarded.insert(fkey);
            hdr.routeFingerprint = MixRouteFingerprint(hdr.routeFingerprint, m_nodeId);
            hdr.routeHopCount++;
            SendUnicastReliable(hdr, it->second.nextHopId);
        }
        return;
    }

    // The destination itself is part of the observed forward route.
    hdr.routeFingerprint = MixRouteFingerprint(hdr.routeFingerprint, m_nodeId);
    hdr.routeHopCount++;

    auto it = m_routingTable.find(hdr.originatorId);
    if (it == m_routingTable.end() || !it->second.valid)
    {
        // We don't know the way back yet; drop (mirrors a real deployment
        // where the reverse route should already exist from discovery).
        return;
    }
    WsnHeader reply;
    reply.type = FrameType::PING_REPLY;
    reply.originatorId = hdr.originatorId;
    reply.targetId = m_nodeId;
    reply.seq = hdr.seq;
    reply.hopCount = 0;
    reply.accumulatedMetric = 0.0;
    reply.routeFingerprint = hdr.routeFingerprint;
    reply.routeHopCount = hdr.routeHopCount;
    SendUnicastReliable(reply, it->second.nextHopId, 0, m_pingPayloadBytes);
}

void
WsnRoutingApp::HandlePingReply(WsnHeader hdr, uint32_t fromId, const WsnLinkInfoTag& /*tag*/)
{
    if (hdr.originatorId != m_nodeId)
    {
        // Intermediate hop: forward toward the true originator via our
        // route table (only relevant if pings ever traverse >1 hop from
        // gateway to gateway, kept for completeness/multi-gateway setups).
        std::string fkey = ForwardKey(hdr);
        if (m_alreadyForwarded.count(fkey))
        {
            // Duplicate delivery (ACK-loss retransmission of a reply we
            // already forwarded) -- see the identical guard in
            // HandlePathDiscoveryReply for why re-forwarding would cause
            // an exponential retry storm.
            return;
        }
        auto it = m_routingTable.find(hdr.originatorId);
        if (it != m_routingTable.end() && it->second.valid && it->second.nextHopId != fromId)
        {
            m_alreadyForwarded.insert(fkey);
            SendUnicastReliable(hdr, it->second.nextHopId);
        }
        return;
    }
    // We are the gateway that sent the original PING. Only the exact
    // (target,seq) transaction may cancel its timeout; a delayed reply for
    // an older ping is otherwise a harmless stale packet.
    auto pingKey = std::make_pair(hdr.targetId, hdr.seq);
    auto timeout = m_pingTimeoutEvents.find(pingKey);
    if (timeout == m_pingTimeoutEvents.end())
    {
        return;
    }
    Simulator::Cancel(timeout->second);
    m_pingTimeoutEvents.erase(timeout);
    m_stats.pingTrace[pingKey] = {"success", hdr.routeFingerprint, hdr.routeHopCount};
}

} // namespace leo
} // namespace ns3
