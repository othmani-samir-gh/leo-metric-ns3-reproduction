#include "wsn-net-device.h"

#include "wsn-channel.h"

#include <ns3/log.h>

namespace ns3
{
namespace leo
{

NS_LOG_COMPONENT_DEFINE("WsnNetDevice");

// ---------------------------------------------------------------------
// WsnLinkInfoTag
// ---------------------------------------------------------------------
TypeId
WsnLinkInfoTag::GetTypeId()
{
    static TypeId tid = TypeId("ns3::leo::WsnLinkInfoTag")
                             .SetParent<Tag>()
                             .SetGroupName("LeoWsn")
                             .AddConstructor<WsnLinkInfoTag>();
    return tid;
}

TypeId
WsnLinkInfoTag::GetInstanceTypeId() const
{
    return GetTypeId();
}

void
WsnLinkInfoTag::Serialize(TagBuffer i) const
{
    i.WriteDouble(rssiDbm);
    i.WriteDouble(snrDb);
    i.WriteDouble(txPowerDbm);
    i.WriteU8(retransmissionCount);
    i.WriteU8(isRetransmission ? 1 : 0);
}

void
WsnLinkInfoTag::Deserialize(TagBuffer i)
{
    rssiDbm = i.ReadDouble();
    snrDb = i.ReadDouble();
    txPowerDbm = i.ReadDouble();
    retransmissionCount = i.ReadU8();
    isRetransmission = i.ReadU8() != 0;
}

uint32_t
WsnLinkInfoTag::GetSerializedSize() const
{
    return 3 * sizeof(double) + 2 * sizeof(uint8_t);
}

void
WsnLinkInfoTag::Print(std::ostream& os) const
{
    os << "rssi=" << rssiDbm << "dBm snr=" << snrDb << "dB txPower=" << txPowerDbm
       << "dBm retx=" << static_cast<int>(retransmissionCount);
}

// ---------------------------------------------------------------------
// WsnNetDevice
// ---------------------------------------------------------------------
TypeId
WsnNetDevice::GetTypeId()
{
    static TypeId tid = TypeId("ns3::leo::WsnNetDevice")
                             .SetParent<Object>()
                             .SetGroupName("LeoWsn")
                             .AddConstructor<WsnNetDevice>();
    return tid;
}

WsnNetDevice::WsnNetDevice()
{
}

void
WsnNetDevice::DoDispose()
{
    m_receiveCallback = WsnReceiveCallback();
    m_channel = nullptr;
    Object::DoDispose();
}

void
WsnNetDevice::SetChannel(Ptr<WsnChannel> channel)
{
    m_channel = channel;
}

Ptr<WsnChannel>
WsnNetDevice::GetChannel() const
{
    return m_channel;
}

void
WsnNetDevice::SetAddress(Mac48Address addr)
{
    m_address = addr;
}

Mac48Address
WsnNetDevice::GetAddress() const
{
    return m_address;
}

void
WsnNetDevice::SetNodeId(uint32_t nodeId)
{
    m_nodeId = nodeId;
}

uint32_t
WsnNetDevice::GetNodeId() const
{
    return m_nodeId;
}

void
WsnNetDevice::SetTxPowerDbm(double dbm)
{
    m_txPowerDbm = dbm;
}

double
WsnNetDevice::GetTxPowerDbm() const
{
    return m_txPowerDbm;
}

void
WsnNetDevice::SetMaxTxPowerDbm(double dbm)
{
    m_maxTxPowerDbm = dbm;
}

double
WsnNetDevice::GetMaxTxPowerDbm() const
{
    return m_maxTxPowerDbm;
}

void
WsnNetDevice::SetReceiveCallback(WsnReceiveCallback cb)
{
    m_receiveCallback = cb;
}

void
WsnNetDevice::Send(Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this << packet);
    if (m_channel)
    {
        m_channel->Send(Ptr<WsnNetDevice>(this), packet, m_txPowerDbm);
    }
}

void
WsnNetDevice::SendTo(Ptr<Packet> packet, uint32_t intendedNextHopId)
{
    NS_LOG_FUNCTION(this << packet << intendedNextHopId);
    if (m_channel)
    {
        m_channel->SendUnicast(Ptr<WsnNetDevice>(this), packet, m_txPowerDbm, intendedNextHopId);
    }
}

void
WsnNetDevice::Receive(Ptr<Packet> packet, Mac48Address from, WsnLinkInfoTag tag)
{
    if (!m_receiveCallback.IsNull())
    {
        m_receiveCallback(packet, from, tag);
    }
}

double
WsnNetDevice::GetEnergyConsumedMWs() const
{
    return m_energyConsumedMWs;
}

void
WsnNetDevice::AddEnergyMWs(double mws)
{
    m_energyConsumedMWs += mws;
}

} // namespace leo
} // namespace ns3
