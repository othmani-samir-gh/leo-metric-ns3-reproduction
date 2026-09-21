/*
 * neighbor-table.h
 *
 * Implements the "Neighbor Table" described in Section 3.2 of:
 *   M. Fitos, J. Dudak, G. Gaspar, J. Machaj, P. Brida,
 *   "LEO: An innovative metric for energy-efficient routing in wireless
 *   sensor networks," Internet of Things, vol. 29, 101472, 2025.
 *
 * Each row describes one reachable neighboring node and stores the columns
 * listed by the authors:
 *   - Link signal loss [dB]            (LSL)      - Eq. (6)
 *   - Max neighboring node TXP [dBm]   (MaxTXP)   - static, learned once
 *   - Retransmission count RX          (R_RX)     - EMA, range [0,6]
 *   - Retransmission count TX          (R_TX)     - EMA, range [0,6]
 *   - Required TXP [dBm]               (ReqTXP)   - set by ATPC
 *
 * This file intentionally sticks to the paper's terminology and units so
 * that every quantity here can be traced back to a specific equation.
 */
#ifndef LEO_NEIGHBOR_TABLE_H
#define LEO_NEIGHBOR_TABLE_H

#include <cstdint>
#include <map>
#include <ns3/ipv4-address.h>

namespace ns3
{
namespace leo
{

/**
 * \brief One row of the Neighbor Table (Section 3.2 of the paper).
 */
struct NeighborEntry
{
    // --- Static / rarely-changing fields ---
    double maxTxPowerDbm{0.0};   //!< MaxTXP: neighbor's max achievable TX power [dBm]
    bool maxTxPowerKnown{false}; //!< whether MaxTXP has been discovered yet

    // --- Link quality fields ---
    double linkSignalLossDb{0.0}; //!< LSL, Eq. (6). Always <= 0 by definition in the paper.
    bool lslKnown{false};

    double lastRssiDbm{0.0}; //!< most recently measured RSSI from this neighbor
    bool rssiKnown{false};

    /// Effective SNR reported by the channel for the most recent decoded
    /// reception. Unlike RSSI-noise reconstruction, this already includes
    /// any link-specific interference penalty applied by WsnChannel.
    double lastSnrDb{0.0};
    bool snrKnown{false};

    /// EMA-tracked packet delivery ratio p_l (Eq. 1), updated from actual
    /// ARQ outcomes (ACK received vs. retry/give-up) -- see
    /// NeighborTable::UpdateDeliveryOutcome. This is what drives the
    /// PRIMARY ZigBee-LQI route-selection cost in this implementation;
    /// see route-metrics.h's ZigbeeLqiMetric doc comment for why.
    double deliveryRatioEma{1.0};
    bool deliveryRatioKnown{false};

    // --- Retransmission counters, EMA over [0,6] as per the paper ---
    double rRx{0.0}; //!< Retransmission count RX (EMA of counts observed on reception)
    double rTx{0.0}; //!< Retransmission count TX (EMA of counts observed on transmission)
    bool rRxKnown{false};
    bool rTxKnown{false};

    // --- ATPC state ---
    double reqTxPowerDbm{0.0}; //!< ReqTXP: minimum TX power required for reliable comms
    bool reqTxPowerKnown{false};

    // --- Bookkeeping for one-way link detection, Eq. (12)-(13) ---
    double pTxRequestedByNeighborDbm{0.0}; //!< P_TX,RN as received from the neighbor
    bool pTxRequestedByNeighborKnown{false};
};

/**
 * \brief EMA smoothing factor used for R_RX / R_TX updates.
 *
 * The paper states these are "computed as an exponential moving average
 * (EMA)" but does not publish the smoothing constant alpha. We expose it as
 * a tunable simulation parameter (default 0.2) so it can be swept/reported
 * as part of a sensitivity analysis; this is flagged explicitly in the
 * accompanying README for reproducibility.
 */
constexpr double kDefaultEmaAlpha = 0.2;

/**
 * \brief Maximum retransmission count representable in the header field
 * (paper: "The value of both attributes can range from 0 to 6").
 */
constexpr uint8_t kMaxRetransmissionField = 6;

/**
 * \brief Per-node table mapping neighbor address -> NeighborEntry.
 */
class NeighborTable
{
  public:
    NeighborTable() = default;

    /// Get (creating if necessary) the entry for a given neighbor.
    NeighborEntry& Get(Ipv4Address neighbor);

    /// Whether we have ever heard from this neighbor.
    bool Has(Ipv4Address neighbor) const;

    /// Update R_RX with a freshly observed retransmission count (EMA).
    void UpdateRxRetransmissions(Ipv4Address neighbor, uint8_t observedCount, double alpha = kDefaultEmaAlpha);

    /// Update R_TX with a freshly observed retransmission count (EMA).
    void UpdateTxRetransmissions(Ipv4Address neighbor, uint8_t observedCount, double alpha = kDefaultEmaAlpha);

    /// Update the EMA-tracked delivery ratio p_l (Eq. 1) from one ARQ
    /// outcome: \p success = true if this specific transmission attempt
    /// was ACKed, false if it was not (timed out).
    void UpdateDeliveryOutcome(Ipv4Address neighbor, bool success, double alpha = kDefaultEmaAlpha);

    const std::map<Ipv4Address, NeighborEntry>& GetTable() const
    {
        return m_table;
    }

  private:
    std::map<Ipv4Address, NeighborEntry> m_table;
};

} // namespace leo
} // namespace ns3

#endif // LEO_NEIGHBOR_TABLE_H
