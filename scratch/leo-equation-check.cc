/*
 * leo-equation-check.cc
 *
 * Completes Test 1 of the pre-publication checklist. `equation_oracle.py`
 * verifies that an INDEPENDENT Python re-implementation of the paper's
 * equations matches hand-derived expected values. That alone does NOT
 * prove the actual C++ in contrib/leo-wsn/model/route-metrics.cc agrees
 * with it -- the two were written separately and had never been compared
 * numerically until this program existed.
 *
 * This scratch program calls the REAL C++ functions (the same ones the
 * simulation uses) on the SAME test vectors the Python oracle uses, and
 * prints them in an identical `name=value` format so the two can be
 * diffed mechanically:
 *
 *     ./ns3 run leo-equation-check > cpp_out.txt
 *     python3 equation_oracle.py --emit-values > py_out.txt
 *     diff cpp_out.txt py_out.txt && echo "ORACLE AND C++ AGREE"
 *
 * Any disagreement here is a genuine implementation bug in one of the
 * two and must be resolved before trusting any simulation result.
 */
#include "ns3/core-module.h"

#include "ns3/leo-wsn-module.h"
// If your ns-3 version does not auto-generate the aggregated header,
// replace the include above with:
// #include "ns3/route-metrics.h"
// #include "ns3/neighbor-table.h"

#include <iomanip>
#include <iostream>

using namespace ns3;
using namespace ns3::leo;

static void
Emit(const std::string& name, double value)
{
    // 6 decimal places matches the tolerance equation_oracle.py checks at.
    std::cout << name << "=" << std::fixed << std::setprecision(6) << value << "\n";
}

int
main(int argc, char* argv[])
{
    CommandLine cmd(__FILE__);
    cmd.Parse(argc, argv);

    RadioParameters radio; // defaults: pTxMax=+8dBm, rMax=4, span=15dB, P_R=1mW

    // --- Eq. (1): generic ZigBee cost from delivery probability p_l ---
    Emit("eq1_pl_0.95", ZigbeeLqiMetric::LinkCostFromDeliveryProbability(0.95));
    Emit("eq1_pl_0.5", ZigbeeLqiMetric::LinkCostFromDeliveryProbability(0.5));
    Emit("eq1_pl_0", ZigbeeLqiMetric::LinkCostFromDeliveryProbability(0.0));

    // --- Eq. (2)-(3): nRF52840 LQI, literal ---
    double lqiWeak = ZigbeeLqiMetric::LqiFromRssiDbm(-80.0);
    double lqiStrong = ZigbeeLqiMetric::LqiFromRssiDbm(-50.0);
    Emit("eq3_rssi_-80", lqiWeak);
    Emit("eq2_lqi_12", ZigbeeLqiMetric::LinkCostFromLqi(lqiWeak));
    Emit("eq3_rssi_-50", lqiStrong);
    Emit("eq2_lqi_42", ZigbeeLqiMetric::LinkCostFromLqi(lqiStrong));

    // --- Eq. (15): dBm <-> mW ---
    Emit("eq15_0dbm", DbmToMw(0.0));
    Emit("eq15_8dbm", DbmToMw(8.0));

    // --- Eq. (17): retransmissions from power deficiency ---
    Emit("eq17_delta15", LeoMetric::RetransmissionsFromPowerDeficiency(15.0, 0.0, radio));

    // --- Eq. (16): average retransmissions ---
    Emit("eq16_both_known", LeoMetric::AverageRetransmissions(true, 2.0, true, 4.0, 0.0, radio));
    Emit("eq16_neither_known", LeoMetric::AverageRetransmissions(false, 0.0, false, 0.0, 0.0, radio));

    // --- Eq. (14): link power ---
    Emit("eq14_r0_ptxmax", LeoMetric::LinkPowerMw(0.0, 8.0, radio));

    return 0;
}
