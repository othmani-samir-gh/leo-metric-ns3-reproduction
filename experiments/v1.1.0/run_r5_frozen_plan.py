#!/usr/bin/env python3
import argparse
import csv
import hashlib
import json
import math
import pathlib
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
EXP = ROOT / "experiments/v1.1.0"
RESULTS = ROOT / "results/v1.1.0"
CONFIG_ROOT = RESULTS / "configs"
PROV = RESULTS / "provenance"
EXPECTED_MANIFEST = "f36d0510e3dbb0d3c9adffa565c74ad2c5026b77fa1cf04b411ba46f49f4c520"
EXPECTED_PLAN = "fb6d360f6f1f88ea50e59b91d2ae147e0c22c2c3e9ff195212e8fe89c932428e"

def sha(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()

def load_plan():
    if sha(EXP / "manifest.json") != EXPECTED_MANIFEST:
        raise SystemExit("manifest SHA mismatch")
    if sha(EXP / "frozen_plan.jsonl") != EXPECTED_PLAN:
        raise SystemExit("plan SHA mismatch")
    return [json.loads(x) for x in (EXP / "frozen_plan.jsonl").read_text().splitlines() if x.strip()]

def replace_arg(argv, key, value):
    prefix = f"--{key}="
    out = [a for a in argv if not a.startswith(prefix)]
    out.append(prefix + str(value))
    return out

def arg_value(argv, key):
    prefix = f"--{key}="
    for a in argv:
        if a.startswith(prefix):
            return a[len(prefix):]
    raise KeyError(key)

def read_csv(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))

def validate_config(row, main_path, route_path):
    mains = read_csv(main_path)
    routes = read_csv(route_path)
    runs = int(row["runs"])
    n_nodes = int(float(arg_value(row["argv"], "nNodes")))
    expected_routes = runs * 10 * (n_nodes - 1)
    if len(mains) != runs:
        raise ValueError(f"main rows {len(mains)} != {runs}")
    if len(routes) != expected_routes:
        raise ValueError(f"route rows {len(routes)} != {expected_routes}")
    if {int(r["run"]) for r in mains} != set(range(runs)):
        raise ValueError("main run indices incomplete")
    for r in mains:
        if r["scenarioSet"] != row["scenarioSet"] or r["scenarioId"] != row["scenarioId"]:
            raise ValueError("main scenario identity mismatch")
        if r["manifestSha256"] != EXPECTED_MANIFEST:
            raise ValueError("main manifest SHA mismatch")
        energy = float(r["totalEnergyMWs"])
        if not math.isfinite(energy) or energy < 0:
            raise ValueError("invalid total energy")
        if int(r["pingSent"]) + int(r["pingNoRoute"]) != 10 * (n_nodes - 1):
            raise ValueError("ping attempt invariant failed")

    keys = set()
    per_run = {i: 0 for i in range(runs)}
    for r in routes:
        if r["scenarioSet"] != row["scenarioSet"] or r["scenarioId"] != row["scenarioId"]:
            raise ValueError("route scenario identity mismatch")
        if r["manifestSha256"] != EXPECTED_MANIFEST:
            raise ValueError("route manifest SHA mismatch")
        if r["status"] not in {"success", "timeout", "no_route"}:
            raise ValueError("invalid/pending route status")
        k = (int(r["run"]), int(r["targetId"]), int(r["seq"]))
        if k in keys:
            raise ValueError("duplicate route key")
        keys.add(k)
        per_run[k[0]] += 1
        if r["status"] == "success":
            if int(r["routeFingerprint"]) <= 0 or int(r["routeHopCount"]) <= 0:
                raise ValueError("successful route lacks direct evidence")
    if any(v != 10 * (n_nodes - 1) for v in per_run.values()):
        raise ValueError("per-run route attempt count mismatch")
    return len(mains), len(routes)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=str(ROOT / "build/r5/leo-topologies"))
    ap.add_argument("--set", dest="scenario_set")
    ap.add_argument("--max-configs", type=int)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    plan = load_plan()
    if args.scenario_set:
        plan = [r for r in plan if r["scenarioSet"] == args.scenario_set]
    if args.max_configs is not None:
        plan = plan[:args.max_configs]
    if not plan:
        raise SystemExit("no plan rows selected")

    build_receipt_path = PROV / "build_receipt.json"
    if not build_receipt_path.exists():
        raise SystemExit("missing R5 build receipt; run build_r5_binary.sh first")
    build_receipt = json.loads(build_receipt_path.read_text())
    binary = pathlib.Path(args.binary)
    if sha(binary) != build_receipt["binary_sha256"]:
        raise SystemExit("binary SHA mismatch against build receipt")

    print(f"R5_SELECTED_CONFIGS={len(plan)}")
    if args.dry_run:
        for r in plan[:5]:
            print(r["scenarioSet"], r["scenarioId"])
        print("R5_DRY_RUN=PASS")
        return

    completed = 0
    skipped = 0
    for idx, row in enumerate(plan, 1):
        cdir = CONFIG_ROOT / row["scenarioSet"] / row["scenarioId"]
        cdir.mkdir(parents=True, exist_ok=True)
        main_path = cdir / "main.csv"
        route_path = cdir / "routes.csv"
        receipt_path = cdir / "receipt.json"
        log_path = cdir / "run.log"

        if main_path.exists() or route_path.exists():
            if not (main_path.exists() and route_path.exists()):
                raise SystemExit(f"partial existing config: {cdir}")
            mr, rr = validate_config(row, main_path, route_path)
            if not receipt_path.exists():
                receipt_path.write_text(json.dumps({
                    "status": "RECOVERED_EXISTING_COMPLETE",
                    "scenarioSet": row["scenarioSet"],
                    "scenarioId": row["scenarioId"],
                    "main_rows": mr,
                    "route_rows": rr,
                    "main_sha256": sha(main_path),
                    "routes_sha256": sha(route_path),
                    "manifest_sha256": EXPECTED_MANIFEST,
                    "plan_sha256": EXPECTED_PLAN,
                    "binary_sha256": build_receipt["binary_sha256"]
                }, indent=2, sort_keys=True) + "\n")
            skipped += 1
            print(f"[{idx}/{len(plan)}] SKIP complete {row['scenarioSet']} {row['scenarioId']}")
            continue

        argv = list(row["argv"])
        argv = replace_arg(argv, "outCsv", main_path)
        argv = replace_arg(argv, "routeCsv", route_path)
        cmd = [str(binary), *argv]
        started = time.time()
        with open(log_path, "w") as log:
            cp = subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        elapsed = time.time() - started
        if cp.returncode != 0:
            raise SystemExit(f"configuration failed rc={cp.returncode}: {row['scenarioId']} log={log_path}")

        mr, rr = validate_config(row, main_path, route_path)
        receipt = {
            "status": "PASS",
            "scenarioSet": row["scenarioSet"],
            "scenarioId": row["scenarioId"],
            "manifest_sha256": EXPECTED_MANIFEST,
            "plan_sha256": EXPECTED_PLAN,
            "source_commit": build_receipt["source_commit"],
            "source_tree": build_receipt["source_tree"],
            "ns3_commit": build_receipt["ns3_commit"],
            "binary_sha256": build_receipt["binary_sha256"],
            "main_rows": mr,
            "route_rows": rr,
            "main_sha256": sha(main_path),
            "routes_sha256": sha(route_path),
            "log_sha256": sha(log_path),
            "wall_seconds": elapsed
        }
        receipt_path.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")
        completed += 1
        print(f"[{idx}/{len(plan)}] PASS {row['scenarioSet']} {row['scenarioId']} main={mr} routes={rr}")

    print(f"R5_COMPLETED_THIS_RUN={completed}")
    print(f"R5_SKIPPED_EXISTING={skipped}")
    print("R5_SELECTED_EXECUTION=PASS")

if __name__ == "__main__":
    main()
