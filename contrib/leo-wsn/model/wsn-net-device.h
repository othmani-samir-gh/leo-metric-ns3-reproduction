/*
 * wsn-net-device.h
 *
 * Minimal per-node radio endpoint attached to a WsnChannel. It is *not* a
 * full ns3::NetDevice subclass: the routing application in this module
 * talks to it directly (see wsn-routing-app.h) rather than going through
 * ns-3's Ipv4/L2 stack, because the protocol under study (flooding-based
 * route discovery with an application-computed metric, Section 3.3) is
 * itself implemented at the application layer, exactly as the authors
 * describe their own MeshNet/MeshProtocolSimulator design. This keeps the
 * module small and avoids depending on internal APIs of a specific ns-3
 * PHY/MAC module that do not model this custom protocol anyway.
 *
 * Each successfully delivered packet carries, in a small tag
 * (WsnLinkInfoTag), the physical-layer observables the paper's metrics
 * need: measured RSSI, computed SNR, and whether the *sender* marks the
 * frame as a retransmission (so the receiver can update R_RX, R_TX).
 */
#ifndef LEO_WSN_NET_DEVICE_H
#define LEO_WSN_NET_DEVICE_H

#include <ns3/callback.h>
#include <ns3/mac48-address.h>
#include <ns3/object.h>
#include <ns3/packet.h>
#include <ns3/tag.h>

#include <cstdint>

namespace ns3
{
namespace leo
{

class WsnChannel;

/// Per-packet PHY metadata attached by the channel on successful delivery.
class WsnLinkInfoTag : public Tag
{
  public:
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;

    void Serialize(TagBuffer i) const override;
    void Deserialize(TagBuffer i) override;
    uint32_t GetSerializedSize() const override;
    void Print(std::ostream& os) const override;

    double rssiDbm{0.0};
    double snrDb{0.0};
    double txPowerDbm{0.0};
    /// NOTE: these two fields are never populated by WsnChannel::Send and
    /// are effectively dead/vestigial -- the authoritative retransmission
    /// count for a received frame is `WsnHeader::retransmissionCount`
    /// (already deserialized from the packet itself before this tag is
    /// consulted), not this tag. Kept here only for ABI/serialization
    /// stability; do not read them.
    uint8_t retransmissionCount{0};
    bool isRetransmission{false};
};

/**
 * \brief Callback signature used to hand a received packet + link metadata
 * up to the routing application: (packet, sender MAC, tag).
 */
typedef Callback<void, Ptr<Packet>, Mac48Address, WsnLinkInfoTag> WsnReceiveCallback;

class WsnNetDevice : public Object
{
  public:
    static TypeId GetTypeId();
    WsnNetDevice();

    void SetChannel(Ptr<WsnChannel> channel);
    Ptr<WsnChannel> GetChannel() const;

    void SetAddress(Mac48Address addr);
    Mac48Address GetAddress() const;

    void SetNodeId(uint32_t nodeId);
    uint32_t GetNodeId() const;

    /// Current/last-used TX power in dBm (updated by ATPC).
    void SetTxPowerDbm(double dbm);
    double GetTxPowerDbm() const;

    /// Max TX power this node can reach (MaxTXP), fixed per node.
    void SetMaxTxPowerDbm(double dbm);
    double GetMaxTxPowerDbm() const;

    void SetReceiveCallback(WsnReceiveCallback cb);

    /// Broadcast \p packet at the device's current TX power.
    void Send(Ptr<Packet> packet);

    /// Logical point-to-point send: evaluates delivery only for
    /// \p intendedNextHopId instead of every attached device (see
    /// WsnChannel::SendUnicast's doc comment). Used for all genuinely
    /// unicast traffic (PING/PING_REPLY/PATH_DISCOVERY_REPLY/ACK).
    void SendTo(Ptr<Packet> packet, uint32_t intendedNextHopId);

    /// Invoked by WsnChannel on successful delivery.
    void Receive(Ptr<Packet> packet, Mac48Address from, WsnLinkInfoTag tag);

    /// Energy accounting: mWs consumed so far by this device (TX + RX side).
    double GetEnergyConsumedMWs() const;
    void AddEnergyMWs(double mws);

  private:
    Ptr<WsnChannel> m_channel;
    Mac48Address m_address;
    uint32_t m_nodeId{0};
    double m_txPowerDbm{0.0};
    double m_maxTxPowerDbm{8.0};
    WsnReceiveCallback m_receiveCallback;
    double m_energyConsumedMWs{0.0};
};

} // namespace leo
} // namespace ns3

#endif // LEO_WSN_NET_DEVICE_H
