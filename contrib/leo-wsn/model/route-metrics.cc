#include "route-metrics.h"
#include "wsn-channel.h" // for PrrFromSnrDb, used as an SNR-informed prior for p_l

#include <algorithm>

namespace ns3
{
namespace leo
{

double
IRouteMetric::CombineRoute(const std::vector<double>& linkMetrics) const
{
    // Eq. (11): P_p = sum_{i=1}^{n} P_l_i  -- additive route weight,
    // used identically by Hop-count, ZigBee LQI and LEO in the paper.
    double sum = 0.0;
    for (double v : linkMetrics)
    {
        sum += v;
    }
    return sum;
}

// =======================================================================
// Hop-count
// =======================================================================
LinkMetricResult
HopCountMetric::ComputeLinkMetric(const NeighborEntry& /*entryForPeer*/,
                                   const RadioParameters& /*radio*/) const
{
    // "this metric uses only the number of links/hops on the route" -
    // each link simply contributes 1.
    return LinkMetricResult{1.0, true};
}

// =======================================================================
// ZigBee LQI
// =======================================================================
double
ZigbeeLqiMetric::LinkCostFromDeliveryProbability(double pl)
{
    // Eq. (1)
    if (pl <= 0.0)
    {
        return 7.0;
    }
    double cost = std::round(1.0 / std::pow(pl, 4));
    return std::min(7.0, cost);
}

double
ZigbeeLqiMetric::LqiFromRssiDbm(double rssiDbm)
{
    // Eq. (3): LQI = RSSI + 92
    return rssiDbm + 92.0;
}

double
ZigbeeLqiMetric::LinkCostFromLqi(double lqi)
{
    // Eq. (2): nRF52840-specific weight, C{l} = max(1, LQI/32)
    return std::max(1.0, lqi / 32.0);
}

LinkMetricResult
ZigbeeLqiMetric::ComputeLinkMetric(const NeighborEntry& entryForPeer, const RadioParameters& radio) const
{
    // PRIMARY path: generic ZigBee-specification cost, Eq. (1), driven by
    // an actually-observed delivery ratio p_l (tracked from real ARQ
    // outcomes -- see NeighborTable::UpdateDeliveryOutcome).
    //
    // NOTE ON A MODELING FINDING: the paper also gives an nRF52840-specific
    // variant, Eq. (2)-(3): C{l} = max(1, (RSSI+92)/32). Under a realistic
    // WSN link budget (Section 4's own -64 dBm/m + N=2-3 path loss, any
    // inter-node spacing beyond roughly 1.5 m), RSSI is almost always well
    // below -60 dBm, which makes LQI = RSSI+92 fall below 32 and pins
    // Eq. (2) at its floor of exactly 1.0 for *every* link -- i.e. Eq. (2)
    // becomes numerically identical to Hop-count for any physically
    // plausible deployment distance, which was confirmed empirically while
    // validating this module (Hop-count and "LQI" produced bit-identical
    // simulated outcomes across every layout tested). Eq. (1) does not
    // have this degeneracy, so it is used here as the meaningful ZigBee-LQI
    // baseline; Eq. (2)-(3) remain available as static helpers
    // (LqiFromRssiDbm/LinkCostFromLqi) if you specifically want to
    // reproduce -- and report -- that floor effect instead.
    double pl = m_pl; // last-resort default: truly unknown link
    if (entryForPeer.deliveryRatioKnown)
    {
        // Best available estimate: EMA of actually-observed ARQ outcomes.
        pl = entryForPeer.deliveryRatioEma;
    }
    else if (entryForPeer.rssiKnown)
    {
        // No ACK history yet (e.g. this is the very first flood a node
        // has ever seen from this neighbor -- true for the whole
        // "connection phase" of Section 4, since it runs path discovery
        // exactly once per node). Rather than falling back to one flat
        // constant for every link (which would make Eq. (1) numerically
        // degenerate to Hop-count for that entire phase -- the second
        // modeling pitfall found while validating this module), estimate
        // p_l from the SNR actually measured on this reception, using the
        // same empirical PRR(SNR) relationship the channel itself uses to
        // decide delivery (Fig. 2). This keeps Eq. (1) meaningfully
        // link-quality-sensitive from the very first packet.
        double snrDb = entryForPeer.lastRssiDbm - radio.backgroundNoiseDbm;
        pl = PrrFromSnrDb(snrDb);
    }
    double cost = LinkCostFromDeliveryProbability(pl);
    return LinkMetricResult{cost, true};
}

// =======================================================================
// ZigBee LQI, PAPER-LITERAL Eq. (2)-(3) -- see route-metrics.h's class
// doc comment for the verified directional-inconsistency finding.
// =======================================================================
LinkMetricResult
ZigbeeLqiLiteralMetric::ComputeLinkMetric(const NeighborEntry& entryForPeer,
                                           const RadioParameters& /*radio*/) const
{
    double lqi = entryForPeer.rssiKnown ? ZigbeeLqiMetric::LqiFromRssiDbm(entryForPeer.lastRssiDbm)
                                         : m_fallbackLqi;
    double cost = ZigbeeLqiMetric::LinkCostFromLqi(lqi); // Eq. (2), applied verbatim, no correction
    return LinkMetricResult{cost, true};
}

// =======================================================================
// LEO (proposed metric)
// =======================================================================
double
LeoMetric::RetransmissionsFromPowerDeficiency(double pTxRequiredDbm,
                                               double pTxDbm,
                                               const RadioParameters& radio)
{
    // Eq. (17):
    // R_D = 10^((P_TX,R - P_TX)/15) * (10/9 * R_max) - (10/9 * R_max)
    double deficiencyDb = pTxRequiredDbm - pTxDbm;
    double span = radio.retransmissionDeficiencySpanDb; // "15" in the paper
    double base = (10.0 / 9.0) * radio.rMax;
    double rD = std::pow(10.0, deficiencyDb / span) * base - base;
    // Clamp to the physically sensible range [0, R_max]; a link that is
    // already strong enough (deficiency <= 0) contributes no extra
    // retransmissions from this term.
    return std::clamp(rD, 0.0, static_cast<double>(radio.rMax));
}

double
LeoMetric::AverageRetransmissions(bool rAbKnown,
                                   double rAb,
                                   bool rBaKnown,
                                   double rBa,
                                   double rD,
                                   const RadioParameters& radio)
{
    double avgAbBa;
    if (rAbKnown && rBaKnown)
    {
        avgAbBa = 0.5 * (rAb + rBa); // avg(R_AB, R_BA)
    }
    else if (rAbKnown)
    {
        avgAbBa = rAb; // "only the number in the opposite direction is used"
    }
    else if (rBaKnown)
    {
        avgAbBa = rBa;
    }
    else
    {
        return radio.rMax; // "R is set to the maximum number of retransmissions R_max"
    }
    // Eq. (16): R = max( avg(R_AB, R_BA), R_D )
    return std::max(avgAbBa, rD);
}

bool
LeoMetric::IsBidirectional(double pTxRequestedByNeighborDbm,
                            double pTxMaxDbm,
                            double pThDbm,
                            double pTxMaxNeighborDbm,
                            double linkSignalLossDb,
                            const RadioParameters& radio)
{
    double offset = radio.oneWayOffsetDb; // +10 dB, must be > +5 dB ATPC offset
    // Eq. (12): P_TX,RN - offset <= P_TX,max  -> current node can meet the
    // neighbor's requested power.
    bool cond12 = (pTxRequestedByNeighborDbm - offset) <= pTxMaxDbm;
    // Eq. (13): P_th - offset <= P_TX,max,N + LSL  -> neighbor can meet the
    // power required for us to receive its frames. LSL is <= 0 by
    // definition (signal loss), matching the paper's sign convention.
    bool cond13 = (pThDbm - offset) <= (pTxMaxNeighborDbm + linkSignalLossDb);
    return cond12 && cond13;
}

double
LeoMetric::LinkPowerMw(double rAvgRetransmissions, double pTxDbm, const RadioParameters& radio)
{
    // Eq. (14): P_l = (R * P_TX,max[mW]) + P_TX[mW] + P_R[mW]
    double pTxMaxMw = DbmToMw(radio.pTxMaxDbm);
    double pTxMw = DbmToMw(pTxDbm);
    return (rAvgRetransmissions * pTxMaxMw) + pTxMw + radio.rxPowerPenaltyMw;
}

LinkMetricResult
LeoMetric::ComputeLinkMetric(const NeighborEntry& entryForPeer, const RadioParameters& radio) const
{
    // ReqTXP is clamped to radio.pTxMaxDbm per the paper: "When the ReqTXP
    // is greater than P_TX,max [dBm], the P_TX,max [dBm] value is used."
    double pTxDbm = entryForPeer.reqTxPowerKnown
                        ? std::min(entryForPeer.reqTxPowerDbm, radio.pTxMaxDbm)
                        : radio.pTxMaxDbm;

    // One-way link rejection, Eq. (12)-(13). If we lack the data needed to
    // evaluate the condition, we conservatively treat the link as usable
    // (the paper's checks are opportunistic, run "if the frame cannot be
    // sent" or when the fields are available).
    bool usable = true;
    if (entryForPeer.pTxRequestedByNeighborKnown && entryForPeer.maxTxPowerKnown &&
        entryForPeer.lslKnown)
    {
        double pThDbm = radio.snrSensitivityDb + radio.atpcOffsetDb + radio.backgroundNoiseDbm;
        // Eq. (9): P_th = SNR_th + N + G_RX. Antenna/LNA gain G_RX folded
        // into 0 dB here (see RadioParameters note in README); N is the
        // background noise floor.
        usable = IsBidirectional(entryForPeer.pTxRequestedByNeighborDbm,
                                  radio.pTxMaxDbm,
                                  pThDbm,
                                  entryForPeer.maxTxPowerDbm,
                                  entryForPeer.linkSignalLossDb,
                                  radio);
    }

    double rD = 0.0;
    if (entryForPeer.reqTxPowerKnown)
    {
        rD = RetransmissionsFromPowerDeficiency(entryForPeer.reqTxPowerDbm, pTxDbm, radio);
    }

    double rAvg = AverageRetransmissions(entryForPeer.rRxKnown,
                                          entryForPeer.rRx,
                                          entryForPeer.rTxKnown,
                                          entryForPeer.rTx,
                                          rD,
                                          radio);

    double pLmW = LinkPowerMw(rAvg, pTxDbm, radio);

    return LinkMetricResult{pLmW, usable};
}

} // namespace leo
} // namespace ns3
