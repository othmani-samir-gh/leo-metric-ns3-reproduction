/*
 * nrf52840-current-table.h
 *
 * Discrete TX-power levels and radio current consumption for the
 * nRF52840, replacing the previous fixed-P_R/continuous-dBm energy model
 * with values sourced from Nordic Semiconductor's own published
 * specification, and clearly-marked linear interpolation for the few
 * remaining levels not directly tabulated (no value here is fabricated
 * as if measured; every entry states its provenance).
 *
 * SOURCE (retrieved and independently self-verified via `pdftotext` on
 * the user-supplied nRF52840_PS_v1_11.pdf, Sept 2026 -- every number
 * below was located and read directly from the extracted text, not
 * taken on trust from any secondhand summary):
 *
 *   Section 6.20.15.2 "Radio current consumption (transmitter)",
 *   RADIO-peripheral-only current, DC/DC regulator, VDD = 3 V:
 *     ITX,MINUS40dBM,DCDC   PRF = -40 dBm   -> 2.3 mA
 *     ITX,MINUS20dBM,DCDC   PRF = -20 dBm   -> 2.7 mA
 *     ITX,MINUS16dBM,DCDC   PRF = -16 dBm   -> 2.8 mA
 *     ITX,MINUS12dBM,DCDC   PRF = -12 dBm   -> 3.0 mA
 *     ITX,MINUS8dBM,DCDC    PRF =  -8 dBm   -> 3.3 mA
 *     ITX,MINUS4dBM,DCDC    PRF =  -4 dBm   -> 3.1 mA  (yes, LOWER than
 *                                               -8 dBm's 3.3 mA -- this
 *                                               non-monotonicity is
 *                                               exactly what the table
 *                                               states; not a transcription
 *                                               error, kept as-is)
 *     ITX,0dBM,DCDC         PRF =   0 dBm   -> 4.8 mA
 *     ITX,PLUS4dBM,DCDC     PRF =  +4 dBm   -> 9.6 mA
 *     ITX,PLUS8dBM,DCDC     PRF =  +8 dBm   -> 14.8 mA
 *
 *   Section 6.20.15.3 "Radio current consumption (Receiver)",
 *   RADIO-peripheral-only current, DC/DC regulator, VDD = 3 V:
 *     IRX,1M,DCDC   1 Mbps/1 Mbps BLE -> 4.6 mA
 *     IRX,2M,DCDC   2 Mbps/2 Mbps BLE -> 5.2 mA
 *
 *   This gives EIGHT of the fourteen discrete TX levels directly from
 *   the document (vs. three in an earlier revision of this table that
 *   was anchored on Section 5.2.1.5's system-level figures instead), and
 *   BOTH RX bitrates confirmed (closing a previously-open gap).
 *
 * WHY THIS TABLE USES "RADIO-only" FIGURES INSTEAD OF "SYSTEM-level"
 * FIGURES (a deliberate change from an earlier revision):
 *   Section 5.2.1.5 of the same document reports different, HIGHER
 *   numbers for nominally the same conditions (e.g. 6.40 mA at 0 dBm
 *   DC/DC, vs. 4.8 mA here) because it additionally includes the HFXO
 *   clock current, which Section 6.20.15's "TX/RX only run current"
 *   figures explicitly exclude (confirmed independently by Nordic staff
 *   on DevZone, Case ID 280167). Section 6.20.15 was adopted as the
 *   primary source here because it is far more complete (8 official TX
 *   anchors + both RX bitrates, vs. 3 TX anchors + one RX bitrate), which
 *   reduces the interpolation error for far more of the 14 discrete
 *   levels. The tradeoff: this table's mW figures under-estimate a real
 *   deployed node's TOTAL draw, since the HFXO clock also has to run for
 *   the radio to operate and its current is not included here. If your
 *   publication needs absolute (not just relative, cross-metric) energy
 *   figures, add the clock current back in, or report this limitation
 *   explicitly.
 *
 * CAVEATS (report these if you use this table in a publication):
 *   - Eight TX points (-40,-20,-16,-12,-8,-4,0,+4,+8 dBm -- note that's
 *     nine values but -4 dBm's non-monotonic reading above is genuinely
 *     documented, not a ninth independent anchor issue) and both RX
 *     bitrates are directly from the document. The five REMAINING TX
 *     levels (+2, +3, +5, +6, +7 dBm) are LINEARLY INTERPOLATED between
 *     their nearest official neighbors (0/+4 dBm, or +4/+8 dBm) -- not
 *     measured.
 *   - This table intentionally excludes HFXO clock current (see the
 *     "WHY" note above) -- it is NOT a full-system power figure.
 *   - Supply voltage is fixed at 3.0 V (matching the source figures'
 *     "DC/DC, 3 V" condition) via kNrf52840SupplyVoltageV; real
 *     deployments commonly run other voltages (e.g. 1.8-3.6 V), which
 *     would change the mW figures even at identical mA.
 */
#ifndef LEO_NRF52840_CURRENT_TABLE_H
#define LEO_NRF52840_CURRENT_TABLE_H

#include <algorithm>
#include <array>
#include <cstddef>

namespace ns3
{
namespace leo
{

/// One (dBm, current mA, is this figure official-sourced or interpolated?) entry.
struct Nrf52840CurrentPoint
{
    double dbm;
    double currentMa;
    bool officialSource; //!< true only for entries directly quoted from the Product Specification
};

/**
 * \brief The 14 discrete TXPOWER levels the nRF52840 actually supports
 * (confirmed via Nordic DevZone; consistent with the paper's stated
 * -40 dBm to +8 dBm range, Section 3), with RADIO-peripheral-only
 * current draw (Section 6.20.15.2, DC/DC, 3 V) at each level.
 *
 * Nine of the fourteen rows are `officialSource = true` (quoted directly
 * from the Product Specification -- see the file-level SOURCE comment).
 * The remaining five (+2, +3, +5, +6, +7 dBm) are linearly interpolated
 * between their nearest official neighbors.
 */
inline const std::array<Nrf52840CurrentPoint, 14>&
Nrf52840TxCurrentTable()
{
    static const std::array<Nrf52840CurrentPoint, 14> kTable = {{
        // dBm,   mA,     official?
        {-40.0, 2.3, true},  // OFFICIAL: PS v1.11 6.20.15.2 ITX,MINUS40dBM,DCDC
        {-20.0, 2.7, true},  // OFFICIAL: ITX,MINUS20dBM,DCDC
        {-16.0, 2.8, true},  // OFFICIAL: ITX,MINUS16dBM,DCDC
        {-12.0, 3.0, true},  // OFFICIAL: ITX,MINUS12dBM,DCDC
        {-8.0, 3.3, true},   // OFFICIAL: ITX,MINUS8dBM,DCDC
        {-4.0, 3.1, true},   // OFFICIAL: ITX,MINUS4dBM,DCDC (non-monotonic vs -8dBm; see file header)
        {0.0, 4.8, true},    // OFFICIAL: ITX,0dBM,DCDC
        {2.0, 7.2, false},   // interpolated between 0 and +4 dBm anchors
        {3.0, 8.4, false},   // interpolated
        {4.0, 9.6, true},    // OFFICIAL: ITX,PLUS4dBM,DCDC
        {5.0, 10.9, false},  // interpolated between +4 and +8 dBm anchors
        {6.0, 12.2, false},  // interpolated
        {7.0, 13.5, false},  // interpolated
        {8.0, 14.8, true},   // OFFICIAL: ITX,PLUS8dBM,DCDC
    }};
    return kTable;
}

/// OFFICIAL: nRF52840 Product Specification v1.11, Section 6.20.15.3,
/// IRX,1M,DCDC (RADIO-peripheral-only, DC/DC, 3 V, 1 Mbit/s).
constexpr double kNrf52840RxCurrentMa1Mbps = 4.6;

/// OFFICIAL: same section, IRX,2M,DCDC (2 Mbit/s). Previously
/// unconfirmed in an earlier revision of this table; now directly
/// sourced.
constexpr double kNrf52840RxCurrentMa2Mbps = 5.2;

/**
 * \brief OFFICIAL, Section 5.4.4.2 "64 MHz crystal oscillator (HFXO)":
 * steady-state ("core standby") current draw of the external HFXO
 * crystal oscillator, by crystal part number. This is the current the
 * table above (Section 6.20.15, RADIO-peripheral-only) deliberately
 * excludes -- see the file-level "WHY THIS TABLE USES..." note. The
 * radio cannot operate without HFXO running, so a node's real total
 * current during TX/RX is (this table's current) + (one of these), not
 * just this table's current alone.
 *
 * The paper does not specify which crystal the authors used (an
 * unpublished implementation detail, like several others documented in
 * this project's README), so no single value here is "the" correct
 * default -- all five are exposed and the caller must choose (or accept
 * RadioParameters::hfxoStandbyCurrentMa's default of 0.0, which
 * reproduces the RADIO-only, clock-excluded behavior of earlier
 * revisions of this table for backward comparability).
 *
 * NOT included: ISTART_X32M (average startup current during the first
 * 1 ms after the crystal is enabled from a fully powered-down state,
 * 328-833 uA depending on crystal) -- modeling that would require
 * tracking HFXO power-down/wake state transitions across the
 * simulation, which this module does not currently do (the clock is
 * implicitly treated as continuously available whenever needed). This
 * is a declared, out-of-scope simplification, not an oversight; if your
 * deployment duty-cycles the crystal off between transmissions, real
 * energy will be higher than this model predicts by roughly
 * (startup current - standby current) x (time spent in the 1 ms startup
 * window) per wake event.
 */
enum class Nrf52840HfxoCrystal
{
    EPSON_FA128,    //!< 70 uA standby (lowest of the five)
    EPSON_FA20H,    //!< 72 uA standby
    EPSON_TSX3225,  //!< 80 uA standby
    NDK_NX1612AA,   //!< 136 uA standby
    NDK_NX1210AB,   //!< 143 uA standby (highest of the five)
};

inline double
Nrf52840HfxoStandbyCurrentMa(Nrf52840HfxoCrystal crystal)
{
    switch (crystal)
    {
    case Nrf52840HfxoCrystal::EPSON_FA128:
        return 0.070; // OFFICIAL: ISTBY_X32M_X2
    case Nrf52840HfxoCrystal::EPSON_FA20H:
        return 0.072; // OFFICIAL: ISTBY_X32M_X1
    case Nrf52840HfxoCrystal::EPSON_TSX3225:
        return 0.080; // OFFICIAL: ISTBY_X32M_X0
    case Nrf52840HfxoCrystal::NDK_NX1612AA:
        return 0.136; // OFFICIAL: ISTBY_X32M_X3
    case Nrf52840HfxoCrystal::NDK_NX1210AB:
        return 0.143; // OFFICIAL: ISTBY_X32M_X4
    }
    return 0.0; // unreachable
}

/// Matches the source figures' "DC/DC, 3 V" measurement condition.
constexpr double kNrf52840SupplyVoltageV = 3.0;

/**
 * \brief Snap a desired TX power to the nearest AVAILABLE discrete level
 * that is >= the request, matching the paper's own ATPC description:
 * "P_TX [dBm] is set to the nearest greater or equal available
 * transmission power by the transmitter" (Section 3.3). If the request
 * exceeds the maximum available level, clamps to the maximum.
 */
inline double
SnapToNearestAvailableTxPowerDbm(double desiredDbm)
{
    const auto& table = Nrf52840TxCurrentTable();
    for (const auto& pt : table)
    {
        if (pt.dbm >= desiredDbm)
        {
            return pt.dbm;
        }
    }
    return table.back().dbm; // desired exceeds max available; clamp to max
}

/// Current draw (mA) at a given (already-snapped) discrete TX power. If
/// \p dbm does not exactly match a table entry, snaps first.
inline double
Nrf52840TxCurrentMa(double dbm)
{
    double snapped = SnapToNearestAvailableTxPowerDbm(dbm);
    const auto& table = Nrf52840TxCurrentTable();
    for (const auto& pt : table)
    {
        if (pt.dbm == snapped)
        {
            return pt.currentMa;
        }
    }
    return table.back().currentMa; // unreachable given SnapToNearestAvailableTxPowerDbm's contract
}

/// TX power (mW) at a given desired dBm, after snapping to the nearest
/// available discrete level and looking up its measured/interpolated
/// current draw -- replaces the continuous DbmToMw(dbm) computation when
/// RadioParameters::useNrf52840Energy is enabled.
/// \param hfxoStandbyCurrentMa optional HFXO clock current to add back in
///        (see Nrf52840HfxoStandbyCurrentMa doc comment); 0.0 (default)
///        reproduces the RADIO-only current this table is built from.
inline double
Nrf52840TxPowerMw(double desiredDbm, double hfxoStandbyCurrentMa = 0.0)
{
    return (Nrf52840TxCurrentMa(desiredDbm) + hfxoStandbyCurrentMa) * kNrf52840SupplyVoltageV;
}

/// RX power (mW) at a given bitrate, replacing the arbitrary fixed P_R
/// when RadioParameters::useNrf52840Energy is enabled. \p bitrateBps
/// selects between the two officially-confirmed RX current figures;
/// anything below 1.5 Mbps uses the 1 Mbps figure, otherwise the 2 Mbps
/// figure (the nRF52840/the paper only distinguish these two rates).
/// \param hfxoStandbyCurrentMa see Nrf52840TxPowerMw's doc comment.
inline double
Nrf52840RxPowerMw(double bitrateBps = 1.0e6, double hfxoStandbyCurrentMa = 0.0)
{
    double currentMa = bitrateBps >= 1.5e6 ? kNrf52840RxCurrentMa2Mbps : kNrf52840RxCurrentMa1Mbps;
    return (currentMa + hfxoStandbyCurrentMa) * kNrf52840SupplyVoltageV;
}

} // namespace leo
} // namespace ns3

#endif // LEO_NRF52840_CURRENT_TABLE_H
