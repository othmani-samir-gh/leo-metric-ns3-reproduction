/*
 * route-metrics.h
 *
 * Implements the three link/route metrics compared in the paper:
 *   - Hop-count                (Section 2, "Hop-count metric")
 *   - ZigBee LQI path-cost     (Section 2, Eq. (1)-(3))
 *   - LEO (proposed)           (Section 3.3, Eq. (11), (14)-(17))
 *
 * All three expose the same small interface (IRouteMetric) so that the
 * routing application (wsn-routing-app.h) can be parameterized by metric
 * type, mirroring how Section 4 of the paper runs the *same* simulation
 * scenarios once per metric for a fair comparison.
 *
 * Every equation below carries a comment with its number in the paper.
 */
#ifndef LEO_ROUTE_METRICS_H
#define LEO_ROUTE_METRICS_H

#include "neighbor-table.h"

#include <cmath>
#include <vector>
#include <ns3/ipv4-address.h>

namespace ns3
{
namespace leo
{

/// Which metric a node/route should be evaluated with.
enum class MetricType
{
    HOP_COUNT,
    ZIGBEE_LQI,
    ZIGBEE_LQI_LITERAL, //!< paper Eq. (2)-(3) applied exactly as published, no correction
    LEO
};

/**
 * \brief Convert a power value from dBm to mW.  Eq. (15).
 */
inline double
DbmToMw(double dbm)
{
    return std::pow(10.0, dbm / 10.0);
}

inline double
MwToDbm(double mw)
{
    return 10.0 * std::log10(mw);
}

/**
 * \brief Parameters that are constant for a given radio / deployment and
 * are needed by one or more metrics. These correspond to the nRF52840
 * platform constants discussed in Section 3 and Section 3.1 of the paper.
 */
struct RadioParameters
{
    double pTxMaxDbm{8.0};     //!< max TX power the module can reach (paper: up to +8 dBm)
    double pTxMinDbm{-40.0};   //!< min TX power (paper: down to -40 dBm)
    double snrSensitivityDb{24.0}; //!< SNR_s, measured in Section 3.1, Fig. 2
    double atpcOffsetDb{5.0};      //!< +5 dB offset in Eq. (8)
    double oneWayOffsetDb{10.0};   //!< +10 dB offset in Eq. (12)-(13)
    double backgroundNoiseDbm{-116.0}; //!< N in Eq. (5); noise floor consistent with
                                        //!< SNR_s = +24 dB and a realistic ~-90 dBm
                                        //!< sensitivity. MUST be kept in sync with
                                        //!< WsnChannel::SetBackgroundNoiseDbm() -- see
                                        //!< the wiring in scratch/leo-topologies.cc.
    double rxPowerPenaltyMw{1.0};  //!< P_R in Eq. (14), set to 1 mW during the paper's simulation
    uint8_t rMax{4};               //!< R_max in Eq. (16)-(17), paper uses 4
    double retransmissionDeficiencySpanDb{15.0}; //!< the "15" constant in Eq. (17)

    /// If true, TX/RX energy accounting uses the discrete nRF52840
    /// current table (nrf52840-current-table.h) instead of the
    /// continuous DbmToMw(dbm)/rxPowerPenaltyMw model: TX power is first
    /// snapped to the nearest available discrete level (matching the
    /// paper's own ATPC description, Section 3.3) and its current draw
    /// looked up (official Nordic figures at 0/+8 dBm and RX; linearly
    /// interpolated elsewhere -- see nrf52840-current-table.h's
    /// file-level caveats before citing absolute numbers from this mode).
    bool useNrf52840Energy{false};

    /// HFXO clock standby current (mA) to add back into
    /// Nrf52840TxPowerMw/Nrf52840RxPowerMw's RADIO-only current when
    /// useNrf52840Energy is true. Default 0.0 reproduces prior
    /// RADIO-only-current behavior (excludes the clock, matching Section
    /// 6.20.15's own scope). Set via one of the officially-documented
    /// crystal options in nrf52840-current-table.h's
    /// Nrf52840HfxoStandbyCurrentMa() (e.g.
    /// Nrf52840HfxoStandbyCurrentMa(Nrf52840HfxoCrystal::NDK_NX1612AA))
    /// if you want a closer approximation of total system current; the
    /// paper does not specify which crystal was used, so no single
    /// choice is "correct" -- report whichever you pick explicitly.
    double hfxoStandbyCurrentMa{0.0};
};

/**
 * \brief Result of a link metric computation, generic enough to be summed
 * along a route (Eq. (11): P_p = sum_i P_l_i) or interpreted as ZigBee's
 * C{l} path-cost, or as a hop increment.
 */
struct LinkMetricResult
{
    double value{0.0};       //!< link weight in the metric's native unit
    bool linkUsable{true};   //!< false if the link was rejected (e.g., one-way link, Eq. 12-13)
};

/// Common interface implemented by all three metrics.
class IRouteMetric
{
  public:
    virtual ~IRouteMetric() = default;

    /// Compute the metric of a single link A->B given B's neighbor-table entry for A.
    virtual LinkMetricResult ComputeLinkMetric(const NeighborEntry& entryForPeer,
                                                const RadioParameters& radio) const = 0;

    /// Combine link metrics into a route metric. All three metrics in the
    /// paper are additive (Eq. (11): "sum of the metrics of the individual
    /// links"), so the default implementation is a plain sum; kept virtual
    /// in case a subclass needs a different combination rule.
    virtual double CombineRoute(const std::vector<double>& linkMetrics) const;

    virtual MetricType Type() const = 0;
};

// ---------------------------------------------------------------------
// Hop-count metric (Section 2, "Hop-count metric")
// ---------------------------------------------------------------------
class HopCountMetric : public IRouteMetric
{
  public:
    LinkMetricResult ComputeLinkMetric(const NeighborEntry& entryForPeer,
                                        const RadioParameters& radio) const override;
    MetricType Type() const override
    {
        return MetricType::HOP_COUNT;
    }
};

// ---------------------------------------------------------------------
// ZigBee LQI path-cost metric, Eq. (1)-(3)
// ---------------------------------------------------------------------
class ZigbeeLqiMetric : public IRouteMetric
{
  public:
    /**
     * \param fallbackDeliveryProbability p_l used ONLY when no ARQ delivery
     * sample is yet available for a neighbor (brand-new/unknown link,
     * where the paper itself falls back to C{l}=7 for unknown p_l).
     */
    explicit ZigbeeLqiMetric(double fallbackDeliveryProbability)
        : m_pl(fallbackDeliveryProbability)
    {
    }

    /// Direct implementation of Eq. (1): C{l} = 7 if p_l unknown, else
    /// min(7, round(1/p_l^4)).
    static double LinkCostFromDeliveryProbability(double pl);

    /// Eq. (3): LQI = RSSI + 92 (nRF52840-specific remapping, Section 2).
    static double LqiFromRssiDbm(double rssiDbm);

    /// Eq. (2): C{l} = max(1, LQI/32) (nRF52840-specific weight used by the
    /// authors instead of the generic ZigBee Eq. (1)).
    static double LinkCostFromLqi(double lqi);

    LinkMetricResult ComputeLinkMetric(const NeighborEntry& entryForPeer,
                                        const RadioParameters& radio) const override;
    MetricType Type() const override
    {
        return MetricType::ZIGBEE_LQI;
    }

  private:
    double m_pl;
};

// ---------------------------------------------------------------------
// ZigBee LQI, PAPER-LITERAL Eq. (2)-(3), no correction applied
// ---------------------------------------------------------------------
/**
 * \brief Applies Eq. (2)-(3) exactly as published, with NO correction of
 * any kind -- kept as a deliberately separate, clearly-labeled metric
 * (rather than folded into ZigbeeLqiMetric above, which uses the generic
 * Eq. (1) as its primary path) so that "what happens if we apply the
 * paper's own nRF52840-specific equations literally" can be reported as
 * its own result rather than silently substituted away.
 *
 * IMPORTANT, VERIFIED FINDING: this metric is not merely degenerate at
 * long range (the max(1,...) floor discussed at ZigbeeLqiMetric's doc
 * comment) -- it has a DIRECTIONAL inconsistency even where the floor
 * does not trigger. Worked example: RSSI=-80 dBm (a WEAK link) gives
 * LQI=12, C_l=max(1,12/32)=1.0; RSSI=-50 dBm (a STRONG link) gives
 * LQI=42, C_l=max(1,42/32)=1.3125. The physically BETTER link receives
 * the HIGHER cost. Since every metric here is additive with "lower
 * accumulated cost wins" route selection (Eq. 11), applying Eq. (2)-(3)
 * literally therefore systematically PREFERS weaker links whenever they
 * are not already floored at 1.0. This appears to stem from an internal
 * inconsistency in the paper itself: Section 2 states "LQI can range
 * from 0 (best quality) to 255 (worst quality)", but Eq. (3)'s LQI =
 * RSSI + 92 is an INCREASING function of signal strength, i.e. stronger
 * signals produce HIGHER LQI values, which by the paper's own "0=best"
 * convention would need to correspond to LOWER cost, not higher --
 * Eq. (2)'s C_l = max(1, LQI/32) does the opposite (cost increases with
 * LQI). No correction is applied here on purpose; report this finding
 * verbatim if you run this metric variant, and see route-metrics.h's
 * top-level module comment / the project README for the (deliberately
 * unresolved) discussion of what a principled correction would need to
 * establish before being implemented.
 */
class ZigbeeLqiLiteralMetric : public IRouteMetric
{
  public:
    /// \param fallbackLqi LQI used ONLY when no RSSI sample is yet
    /// available for a neighbor (paper gives no fallback for Eq. 2-3
    /// specifically; using the worst-quality value, 255, is the most
    /// defensible conservative default absent guidance).
    explicit ZigbeeLqiLiteralMetric(double fallbackLqi = 255.0)
        : m_fallbackLqi(fallbackLqi)
    {
    }

    LinkMetricResult ComputeLinkMetric(const NeighborEntry& entryForPeer,
                                        const RadioParameters& radio) const override;
    MetricType Type() const override
    {
        return MetricType::ZIGBEE_LQI_LITERAL;
    }

  private:
    double m_fallbackLqi;
};

// ---------------------------------------------------------------------
// LEO metric (proposed), Section 3.3, Eq. (11), (14)-(17)
// ---------------------------------------------------------------------
class LeoMetric : public IRouteMetric
{
  public:
    /**
     * \brief Direct implementation of Eq. (17):
     *   R_D = 10^((P_TX,R - P_TX)/15) * (10/9 * R_max) - (10/9 * R_max)
     *
     * \param pTxRequiredDbm  P_TX,R  required TX power (from Eq. 10 or ReqTXP)
     * \param pTxDbm          P_TX    TX power actually used
     */
    static double RetransmissionsFromPowerDeficiency(double pTxRequiredDbm,
                                                       double pTxDbm,
                                                       const RadioParameters& radio);

    /**
     * \brief Eq. (16): R = max( avg(R_AB, R_BA), R_D ).
     * Either R_AB or R_BA may be "unknown" (use the other only); if both
     * unknown, R = R_max.
     */
    static double AverageRetransmissions(bool rAbKnown,
                                          double rAb,
                                          bool rBaKnown,
                                          double rBa,
                                          double rD,
                                          const RadioParameters& radio);

    /**
     * \brief Eq. (12)-(13): true if the link is usable in both directions.
     */
    static bool IsBidirectional(double pTxRequestedByNeighborDbm,
                                 double pTxMaxDbm,
                                 double pThDbm,
                                 double pTxMaxNeighborDbm,
                                 double linkSignalLossDb,
                                 const RadioParameters& radio);

    /**
     * \brief Eq. (14): P_l = (R * P_TX,max[mW]) + P_TX[mW] + P_R[mW]
     */
    static double LinkPowerMw(double rAvgRetransmissions,
                               double pTxDbm,
                               const RadioParameters& radio);

    LinkMetricResult ComputeLinkMetric(const NeighborEntry& entryForPeer,
                                        const RadioParameters& radio) const override;
    MetricType Type() const override
    {
        return MetricType::LEO;
    }
};

} // namespace leo
} // namespace ns3

#endif // LEO_ROUTE_METRICS_H
