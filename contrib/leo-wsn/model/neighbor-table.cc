#include "neighbor-table.h"

#include <algorithm>

namespace ns3
{
namespace leo
{

NeighborEntry&
NeighborTable::Get(Ipv4Address neighbor)
{
    return m_table[neighbor]; // default-constructs if absent
}

bool
NeighborTable::Has(Ipv4Address neighbor) const
{
    return m_table.find(neighbor) != m_table.end();
}

void
NeighborTable::UpdateRxRetransmissions(Ipv4Address neighbor, uint8_t observedCount, double alpha)
{
    observedCount = std::min<uint8_t>(observedCount, kMaxRetransmissionField);
    NeighborEntry& e = Get(neighbor);
    if (!e.rRxKnown)
    {
        e.rRx = observedCount;
        e.rRxKnown = true;
    }
    else
    {
        e.rRx = alpha * observedCount + (1.0 - alpha) * e.rRx;
    }
}

void
NeighborTable::UpdateTxRetransmissions(Ipv4Address neighbor, uint8_t observedCount, double alpha)
{
    observedCount = std::min<uint8_t>(observedCount, kMaxRetransmissionField);
    NeighborEntry& e = Get(neighbor);
    if (!e.rTxKnown)
    {
        e.rTx = observedCount;
        e.rTxKnown = true;
    }
    else
    {
        e.rTx = alpha * observedCount + (1.0 - alpha) * e.rTx;
    }
}

void
NeighborTable::UpdateDeliveryOutcome(Ipv4Address neighbor, bool success, double alpha)
{
    double sample = success ? 1.0 : 0.0;
    NeighborEntry& e = Get(neighbor);
    if (!e.deliveryRatioKnown)
    {
        e.deliveryRatioEma = sample;
        e.deliveryRatioKnown = true;
    }
    else
    {
        e.deliveryRatioEma = alpha * sample + (1.0 - alpha) * e.deliveryRatioEma;
    }
}

} // namespace leo
} // namespace ns3
