#!/usr/bin/env python3
import hashlib
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent
manifest_path = ROOT / "manifest.json"
plan_path = ROOT / "frozen_plan.jsonl"
manifest_sha = hashlib.sha256(manifest_path.read_bytes()).hexdigest()
rows = [json.loads(x) for x in plan_path.read_text().splitlines() if x.strip()]
errors = []

def req(cond, msg):
    if not cond:
        errors.append(msg)

req(len(rows) == 617, f"expected 617 config rows, got {len(rows)}")
req(sum(r["runs"] for r in rows) == 12340, "expected 12340 simulation rows")
req(all(r["manifestSha256"] == manifest_sha for r in rows), "manifest SHA mismatch")
req(len({(r["scenarioSet"], r["scenarioId"]) for r in rows}) == len(rows),
    "scenarioSet/scenarioId pairs must be unique")
req(all(r["routeCsv"] for r in rows), "route evidence path required")
req(all("--routeCsv=" in " ".join(r["argv"]) for r in rows), "routeCsv CLI missing")
req(all("--relayCap=" in " ".join(r["argv"]) for r in rows), "relayCap CLI missing")
req(all("--manifestSha256=" + manifest_sha in r["argv"] for r in rows),
    "manifest SHA not embedded in argv")

# Production plan may not contain the R4 preflight identity.
req(all(r["scenarioSet"] != "preflight" for r in rows), "preflight leaked into frozen plan")

# Primary is static, includes all relay caps, and never hides interference identity.
primary = [r for r in rows if r["scenarioSet"] == "primary_static_source_constrained"]
req(len(primary) == 420, f"expected 420 primary configs, got {len(primary)}")
argv_text = [" ".join(r["argv"]) for r in primary]
req(all("--mobility=none" in a for a in argv_text), "primary mobility drift")
req({x for a in argv_text for x in ["1","2","3"] if f"--relayCap={x}" in a} == {"1","2","3"},
    "primary relay caps incomplete")
req(all("--boundEq17ToRMax=false" in a for a in argv_text), "primary Eq17 policy drift")
req(all("--useNrf52840Energy=false" in a for a in argv_text), "primary energy mode drift")

print("PLAN_VALID=PASS" if not errors else "PLAN_VALID=FAIL")
print("MANIFEST_SHA256=" + manifest_sha)
print("PLAN_SHA256=" + hashlib.sha256(plan_path.read_bytes()).hexdigest())
print("CONFIG_ROWS=" + str(len(rows)))
print("SIMULATION_ROWS=" + str(sum(r["runs"] for r in rows)))
if errors:
    for e in errors:
        print("ERROR:", e)
    sys.exit(1)
