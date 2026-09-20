#!/usr/bin/env python3
import hashlib
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent
PATH = ROOT / "manifest.json"
m = json.loads(PATH.read_text(encoding="utf-8"))

errors = []
def require(cond, msg):
    if not cond:
        errors.append(msg)

require(m["schema_version"] == "1.0", "schema_version must be 1.0")
require(m["status"] == "FROZEN_DESIGN_NO_PRODUCTION_EXECUTION_YET",
        "manifest status must keep production closed during R4")
require(m["rng"]["runs_per_configuration"] == 20, "runs must be 20")
require(m["rng"]["seed_base"] == 42, "seed base must be frozen to 42")

p = m["primary"]
require(p["role"] == "PRIMARY", "primary role")
require(p["mobility"] == ["none"], "primary must be static")
require(p["layouts"] == ["around","one-side","u-shaped","line","circle","triangle"],
        "primary must contain the six frozen layouts")
require(p["envFactors"] == [2.0,2.25,2.5,2.625,3.0], "primary env factors drift")
require(p["metrics"] == ["hopcount","lqi","lqi-literal","leo"], "metric set drift")
require(p["relayCaps"] == [1,2,3], "relay caps drift")
require(p["boundEq17ToRMax"] is False, "primary Eq17 policy must remain literal")
require(p["useNrf52840Energy"] is False, "primary energy accounting policy drift")

# 6 layouts x 5 env = 30 ordinary cells, plus 5 triangle-interference cells.
cells_per_cap = len(p["layouts"]) * len(p["envFactors"]) + len(p["envFactors"])
primary_rows = cells_per_cap * len(p["relayCaps"]) * len(p["metrics"]) * m["rng"]["runs_per_configuration"]
attempts_per_run = 10 * (m["fixed_reconstruction_parameters"]["nNodes"] - 1)
primary_routes = primary_rows * attempts_per_run
require(cells_per_cap == p["expected_scenario_cells_per_relay_cap"], "primary cell count mismatch")
require(primary_rows == p["expected_simulation_rows"], "primary row count mismatch")
require(primary_routes == p["expected_ping_trace_rows"], "primary route-row count mismatch")

ids = [p["id"]] + [x["id"] for x in m["sensitivity_sets"]]
require(len(ids) == len(set(ids)), "scenario-set ids must be unique")
require(all(x["id"] != p["id"] for x in m["sensitivity_sets"]), "primary duplicated in sensitivity sets")

outs = m["outputs"]
require(outs["require_manifest_sha256_in_every_row"] is True, "manifest SHA required")
require(outs["require_route_evidence_csv"] is True, "route evidence required")
require(outs["require_no_pending_ping_trace_after_run"] is True, "pending routes forbidden")

af = m["analysis_freeze"]
require(af["outcome_dependent_inclusion_filter"] == "FORBIDDEN", "outcome filter must be forbidden")
require(af["route_discrimination_from_energy_equality"] == "FORBIDDEN", "energy route proxy must be forbidden")
require(set(["interference","relayCap","mobility"]).issubset(set(af["never_pool_across"])),
        "critical strata must never be pooled")

# Check stated row counts for compact sensitivity matrices.
byid = {x["id"]: x for x in m["sensitivity_sets"]}
# 3 layouts x 3 env = 9 ordinary + 3 triangle-intf = 12; x3 caps.
eq17_rows = 12 * 3 * 1 * 20
energy_rows = 12 * 3 * 2 * 20
mob_rows = 3 * 3 * 2 * 4 * 1 * 20
ofat_rows = (1 + 2 * len(byid["reconstruction_ofat_sensitivity"]["factors"])) * 20
require(eq17_rows == byid["eq17_bounded_sensitivity"]["expected_simulation_rows"], "Eq17 row count")
require(energy_rows == byid["nrf52840_energy_sensitivity"]["expected_simulation_rows"], "energy sensitivity row count")
require(mob_rows == byid["mobility_extension"]["expected_simulation_rows"], "mobility row count")
require(ofat_rows == byid["reconstruction_ofat_sensitivity"]["expected_simulation_rows"], "OFAT row count")

sha = hashlib.sha256(PATH.read_bytes()).hexdigest()
print("MANIFEST_VALID=PASS" if not errors else "MANIFEST_VALID=FAIL")
print("MANIFEST_SHA256=" + sha)
print("PRIMARY_CELLS_PER_CAP=" + str(cells_per_cap))
print("PRIMARY_SIMULATION_ROWS=" + str(primary_rows))
print("PRIMARY_PING_TRACE_ROWS=" + str(primary_routes))
print("MANDATORY_SENSITIVITY_ROWS=" + str(eq17_rows + energy_rows + ofat_rows))
print("OPTIONAL_MOBILITY_ROWS=" + str(mob_rows))
print("TOTAL_PLANNED_ROWS=" + str(primary_rows + eq17_rows + energy_rows + ofat_rows + mob_rows))
if errors:
    for e in errors:
        print("ERROR:", e)
    sys.exit(1)
