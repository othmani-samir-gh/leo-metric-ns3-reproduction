#!/usr/bin/env python3
"""
equation_oracle.py

Independent re-implementation, in plain Python, of the paper's equations
that the C++ code in contrib/leo-wsn/model/route-metrics.cc implements.
This is Test 1 of the pre-publication checklist: run known inputs
through BOTH this oracle and the C++ code's own unit-testable pieces,
and confirm the outputs agree to floating-point tolerance.

This script covers the equations that are pure functions of a few
scalar inputs (Eq. 1-3, 14-17); it does NOT re-implement the full
routing/ARQ/mobility simulation (that is what leo-topologies.cc's
mobilityDiagnostics / a small sweep already exercise operationally).

Usage:
    python3 equation_oracle.py
    (runs the built-in self-check test vectors and prints PASS/FAIL)

To cross-check against the C++ code directly: pick a few of the test
vectors below, hand-compute or instrument route-metrics.cc to print the
same inputs/outputs (e.g. temporary std::cout in ComputeLinkMetric), and
diff against this script's output for the SAME inputs.
"""
import math

# ---------------------------------------------------------------------
# Eq. (1): generic ZigBee cost from delivery probability p_l
# ---------------------------------------------------------------------
def eq1_cost_from_pl(pl):
    if pl <= 0.0:
        return 7.0
    return min(7.0, round(1.0 / (pl ** 4)))


# ---------------------------------------------------------------------
# Eq. (2)-(3): nRF52840-specific LQI cost (literal, no correction)
# ---------------------------------------------------------------------
def eq3_lqi_from_rssi(rssi_dbm):
    return rssi_dbm + 92.0


def eq2_cost_from_lqi(lqi):
    return max(1.0, lqi / 32.0)


# ---------------------------------------------------------------------
# Eq. (15): dBm <-> mW
# ---------------------------------------------------------------------
def dbm_to_mw(dbm):
    return 10.0 ** (dbm / 10.0)


# ---------------------------------------------------------------------
# Eq. (17): retransmissions from power deficiency
# ---------------------------------------------------------------------
def eq17_rd(p_tx_r_dbm, p_tx_dbm, r_max=4, span_db=15.0):
    delta_p = p_tx_r_dbm - p_tx_dbm
    base = (10.0 / 9.0) * r_max
    rd = (10.0 ** (delta_p / span_db)) * base - base
    return max(0.0, min(r_max, rd))  # matches the documented clamp in LeoMetric::RetransmissionsFromPowerDeficiency


# ---------------------------------------------------------------------
# Eq. (16): average retransmissions
# ---------------------------------------------------------------------
def eq16_r(r_ab, r_ba, r_ab_known, r_ba_known, r_d, r_max=4):
    if r_ab_known and r_ba_known:
        avg = 0.5 * (r_ab + r_ba)
    elif r_ab_known:
        avg = r_ab
    elif r_ba_known:
        avg = r_ba
    else:
        return float(r_max)
    return max(avg, r_d)


# ---------------------------------------------------------------------
# Eq. (14): link power
# ---------------------------------------------------------------------
def eq14_link_power_mw(r_avg, p_tx_dbm, p_tx_max_dbm, p_r_mw=1.0):
    return r_avg * dbm_to_mw(p_tx_max_dbm) + dbm_to_mw(p_tx_dbm) + p_r_mw


# =======================================================================
# Self-check test vectors. Each is (description, computed, expected).
# Expected values are hand-derived from the equations directly (not
# copied from the C++ output), to keep this an INDEPENDENT check.
# =======================================================================
def run_self_checks():
    checks = []

    # Eq. 1: p_l=0.95 -> round(1/0.95^4) = round(1.2277) = 1
    checks.append(("Eq1 p_l=0.95", eq1_cost_from_pl(0.95), 1.0))
    # Eq. 1: p_l=0.5 -> round(1/0.0625) = round(16) = 16 -> clamped to 7
    checks.append(("Eq1 p_l=0.5 (clamped)", eq1_cost_from_pl(0.5), 7.0))
    # Eq. 1: p_l unknown (<=0) -> 7
    checks.append(("Eq1 p_l=0 (unknown)", eq1_cost_from_pl(0.0), 7.0))

    # Eq. 2-3 worked example from the Fourth-round README finding:
    # RSSI=-80dBm -> LQI=12 -> cost=1.0 (floored)
    lqi_weak = eq3_lqi_from_rssi(-80.0)
    checks.append(("Eq3 RSSI=-80dBm -> LQI", lqi_weak, 12.0))
    checks.append(("Eq2 LQI=12 -> cost", eq2_cost_from_lqi(lqi_weak), 1.0))
    # RSSI=-50dBm -> LQI=42 -> cost=1.3125 (the directional-inconsistency example)
    lqi_strong = eq3_lqi_from_rssi(-50.0)
    checks.append(("Eq3 RSSI=-50dBm -> LQI", lqi_strong, 42.0))
    checks.append(("Eq2 LQI=42 -> cost", eq2_cost_from_lqi(lqi_strong), 1.3125))

    # Eq. 15: 0 dBm = 1 mW; +8 dBm ~ 6.3096 mW
    checks.append(("Eq15 0dBm->mW", dbm_to_mw(0.0), 1.0))
    checks.append(("Eq15 8dBm->mW", round(dbm_to_mw(8.0), 4), 6.3096))

    # Eq. 17: deltaP=15dB, R_max=4 -> R_D = 10*R_max (per README's paper-literal note)
    checks.append(("Eq17 deltaP=15dB", round(eq17_rd(15.0, 0.0, r_max=4), 4), 4.0))
    # (clamped to R_max=4 per the documented stability fix; the LITERAL
    # unclamped paper formula would give 10*4=40 here -- see README's
    # "critical note on Eq. 17" discussion for why the clamp is applied.)

    # Eq. 16: both directions known
    checks.append(("Eq16 both known avg", eq16_r(2.0, 4.0, True, True, 0.0), 3.0))
    # Eq. 16: neither known -> R_max
    checks.append(("Eq16 neither known", eq16_r(0.0, 0.0, False, False, 0.0, r_max=4), 4.0))

    # Eq. 14: R=0, P_TX=P_TX,max=8dBm, P_R=1mW
    p_tx_max_mw = dbm_to_mw(8.0)
    expected_14 = 0.0 * p_tx_max_mw + p_tx_max_mw + 1.0
    checks.append(("Eq14 R=0 P_TX=P_TX,max", round(eq14_link_power_mw(0.0, 8.0, 8.0, 1.0), 4), round(expected_14, 4)))

    passed = 0
    for name, got, expected in checks:
        ok = math.isclose(got, expected, rel_tol=1e-6, abs_tol=1e-6)
        status = "PASS" if ok else "FAIL"
        if ok:
            passed += 1
        print(f"[{status}] {name}: got={got} expected={expected}")

    print(f"\n{passed}/{len(checks)} checks passed.")
    return passed == len(checks)


def emit_values():
    """Print the same test vectors in the same `name=value` format as
    scratch/leo-equation-check.cc, so the two can be diffed mechanically
    to prove the Python oracle and the real C++ agree numerically."""
    def emit(name, value):
        print(f"{name}={value:.6f}")

    emit("eq1_pl_0.95", eq1_cost_from_pl(0.95))
    emit("eq1_pl_0.5", eq1_cost_from_pl(0.5))
    emit("eq1_pl_0", eq1_cost_from_pl(0.0))

    lqi_weak = eq3_lqi_from_rssi(-80.0)
    lqi_strong = eq3_lqi_from_rssi(-50.0)
    emit("eq3_rssi_-80", lqi_weak)
    emit("eq2_lqi_12", eq2_cost_from_lqi(lqi_weak))
    emit("eq3_rssi_-50", lqi_strong)
    emit("eq2_lqi_42", eq2_cost_from_lqi(lqi_strong))

    emit("eq15_0dbm", dbm_to_mw(0.0))
    emit("eq15_8dbm", dbm_to_mw(8.0))

    emit("eq17_delta15", eq17_rd(15.0, 0.0, r_max=4, span_db=15.0))

    emit("eq16_both_known", eq16_r(2.0, 4.0, True, True, 0.0, r_max=4))
    emit("eq16_neither_known", eq16_r(0.0, 0.0, False, False, 0.0, r_max=4))

    emit("eq14_r0_ptxmax", eq14_link_power_mw(0.0, 8.0, 8.0, 1.0))


if __name__ == "__main__":
    import sys

    if "--emit-values" in sys.argv:
        emit_values()
        sys.exit(0)

    ok = run_self_checks()
    sys.exit(0 if ok else 1)
