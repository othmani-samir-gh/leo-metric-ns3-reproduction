#!/usr/bin/env python3
import csv
import json
import hashlib
import math
from collections import defaultdict
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[1]
RESULTS = REPO / "results" / "v1.1.0"
CONFIGS = RESULTS / "configs"
OUT = RESULTS / "analysis_r6"
OUT.mkdir(parents=True, exist_ok=True)

BOOT_REPS = 20000
PERM_REPS = 200000
BASE_SEED = 20260920
ENDPOINTS = ["totalEnergyMWs", "pingTimeouts", "pingNoRoute"]
PRIMARY_COMPARATORS = ["hopcount", "lqi"]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def stable_seed(*parts):
    payload = "|".join(map(str, (BASE_SEED,) + parts)).encode()
    return int.from_bytes(hashlib.sha256(payload).digest()[:8], "little") & ((1 << 63) - 1)


def write_csv(path, rows, fields):
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)


def load_main_set(setname):
    rows = []
    for p in sorted((CONFIGS / setname).glob("*/main.csv")):
        with open(p, newline="") as f:
            for r in csv.DictReader(f):
                r["_dir"] = str(p.parent)
                r["envFactor"] = float(r["envFactor"])
                r["interference"] = int(r["interference"])
                r["relayCap"] = int(r["relayCap"])
                r["run"] = int(r["run"])
                for e in ENDPOINTS:
                    r[e] = float(r[e])
                r["pingSent"] = float(r["pingSent"])
                rows.append(r)
    return rows
def cell_key(r):
    return (r["layout"], r["envFactor"], r["interference"], r["relayCap"])


def main_index(rows):
    return {
        (r["layout"], r["envFactor"], r["interference"], r["relayCap"], r["metric"], r["run"]): r
        for r in rows
    }


def config_dir_index(rows, include_mobility=False):
    out = {}
    for r in rows:
        if include_mobility:
            k = (r["mobility"], r["layout"], r["envFactor"], r["interference"], r["relayCap"], r["metric"])
        else:
            k = (r["layout"], r["envFactor"], r["interference"], r["relayCap"], r["metric"])
        out[k] = Path(r["_dir"])
    return out


def primary_cell_arrays(rows, comparator, endpoint):
    idx = main_index(rows)
    cells = sorted({cell_key(r) for r in rows if r["metric"] == "leo"})
    out = {}
    for c in cells:
        layout, env, inter, cap = c
        d = []
        comp = []
        leo = []
        for run in range(20):
            a = idx[(layout, env, inter, cap, "leo", run)][endpoint]
            b = idx[(layout, env, inter, cap, comparator, run)][endpoint]
            d.append(a - b)
            comp.append(b)
            leo.append(a)
        out[c] = {
            "diff": np.asarray(d, dtype=float),
            "comp": np.asarray(comp, dtype=float),
            "leo": np.asarray(leo, dtype=float),
        }
    return out


def hierarchical_bootstrap_numeric(arrays, seed, reps=BOOT_REPS):
    D = np.stack([x["diff"] for x in arrays], axis=0)
    nc, nr = D.shape
    rng = np.random.default_rng(seed)
    vals = np.empty(reps, dtype=float)
    chunk = 500
    for start in range(0, reps, chunk):
        b = min(chunk, reps - start)
        ci = rng.integers(0, nc, size=(b, nc))
        ri = rng.integers(0, nr, size=(b, nc, nr))
        sampled = D[ci[:, :, None], ri]
        vals[start:start+b] = sampled.mean(axis=(1, 2))
    return np.quantile(vals, [0.025, 0.975])


def sign_flip_p(cell_means, seed):
    x = np.asarray(cell_means, dtype=float)
    n = len(x)
    obs = abs(float(x.mean()))
    eps = 1e-15
    if n <= 20:
        total = 1 << n
        ge = 0
        chunk = 8192
        bitpos = np.arange(n, dtype=np.uint64)
        for start in range(0, total, chunk):
            stop = min(total, start + chunk)
            nums = np.arange(start, stop, dtype=np.uint64)[:, None]
            bits = (nums >> bitpos) & 1
            signs = bits.astype(float) * 2.0 - 1.0
            stats = np.abs((signs * x).mean(axis=1))
            ge += int(np.count_nonzero(stats >= obs - eps))
        return ge / total, "exact", total
    rng = np.random.default_rng(seed)
    ge = 0
    done = 0
    chunk = 10000
    while done < PERM_REPS:
        b = min(chunk, PERM_REPS - done)
        signs = rng.integers(0, 2, size=(b, n), dtype=np.int8) * 2 - 1
        stats = np.abs((signs * x).mean(axis=1))
        ge += int(np.count_nonzero(stats >= obs - eps))
        done += b
    return (ge + 1) / (PERM_REPS + 1), "monte_carlo", PERM_REPS
def holm_adjust(rows):
    families = defaultdict(list)
    for i, r in enumerate(rows):
        families[(r["endpoint"], r["comparator"])].append((i, float(r["p_raw"])))
    for items in families.values():
        items.sort(key=lambda z: z[1])
        m = len(items)
        running = 0.0
        for rank, (idx, p) in enumerate(items):
            adj = min(1.0, (m - rank) * p)
            running = max(running, adj)
            rows[idx]["p_holm"] = running
            rows[idx]["significant_holm_0p05"] = int(running < 0.05)


def summarize_primary_numeric(primary_rows, comparators, inferential):
    summaries = []
    cell_rows = []
    for comp in comparators:
        for endpoint in ENDPOINTS:
            cells = primary_cell_arrays(primary_rows, comp, endpoint)
            for c, obj in cells.items():
                layout, env, inter, cap = c
                cell_rows.append({
                    "comparator": comp, "endpoint": endpoint, "layout": layout,
                    "envFactor": env, "interference": inter, "relayCap": cap,
                    "n_runs": len(obj["diff"]),
                    "comparator_mean": float(obj["comp"].mean()),
                    "leo_mean": float(obj["leo"].mean()),
                    "effect_abs_leo_minus_comparator": float(obj["diff"].mean()),
                })
            for cap in [1, 2, 3]:
                for inter in [0, 1]:
                    selected = [obj for c, obj in cells.items() if c[3] == cap and c[2] == inter]
                    if not selected:
                        continue
                    comp_mean = float(np.mean([x["comp"].mean() for x in selected]))
                    leo_mean = float(np.mean([x["leo"].mean() for x in selected]))
                    effect = float(np.mean([x["diff"].mean() for x in selected]))
                    pct = (100.0 * effect / comp_mean) if comp_mean != 0 else float("nan")
                    lo, hi = hierarchical_bootstrap_numeric(
                        selected, stable_seed("boot", comp, endpoint, cap, inter)
                    )
                    row = {
                        "comparator": comp, "endpoint": endpoint, "relayCap": cap,
                        "interference": inter, "n_cells": len(selected), "runs_per_cell": 20,
                        "comparator_mean": comp_mean, "leo_mean": leo_mean,
                        "effect_abs_leo_minus_comparator": effect,
                        "effect_pct_vs_comparator": pct,
                        "ci95_low": float(lo), "ci95_high": float(hi),
                    }
                    if inferential:
                        p, method, nperm = sign_flip_p(
                            [x["diff"].mean() for x in selected],
                            stable_seed("perm", comp, endpoint, cap, inter),
                        )
                        row.update({"p_raw": p, "permutation_method": method, "permutations": nperm})
                    summaries.append(row)
    if inferential:
        holm_adjust(summaries)
    return summaries, cell_rows
def read_routes(path):
    out = {}
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            k = (int(r["run"]), int(r["targetId"]), int(r["seq"]))
            out[k] = (r["status"], int(r["routeFingerprint"]), int(r["routeHopCount"]))
    return out


def route_pair_counts(a, b):
    div = np.zeros(20, dtype=int)
    joint = np.zeros(20, dtype=int)
    status_mismatch = 0
    if set(a) != set(b):
        raise RuntimeError("route key mismatch between paired configurations")
    for k, va in a.items():
        vb = b[k]
        run = k[0]
        if va[0] != vb[0]:
            status_mismatch += 1
        if va[0] == "success" and vb[0] == "success":
            joint[run] += 1
            if va[1] != vb[1]:
                div[run] += 1
    return div, joint, status_mismatch


def hierarchical_bootstrap_route(items, seed, reps=BOOT_REPS):
    DIV = np.stack([x[0] for x in items], axis=0)
    JOINT = np.stack([x[1] for x in items], axis=0)
    nc, nr = DIV.shape
    rng = np.random.default_rng(seed)
    vals = np.empty(reps, dtype=float)
    chunk = 500
    for start in range(0, reps, chunk):
        b = min(chunk, reps - start)
        ci = rng.integers(0, nc, size=(b, nc))
        ri = rng.integers(0, nr, size=(b, nc, nr))
        d = DIV[ci[:, :, None], ri].sum(axis=2)
        j = JOINT[ci[:, :, None], ri].sum(axis=2)
        frac = np.divide(d, j, out=np.full_like(d, np.nan, dtype=float), where=j > 0)
        vals[start:start+b] = np.nanmean(frac, axis=1)
    return np.nanquantile(vals, [0.025, 0.975])


def summarize_primary_routes(primary_rows, comparators):
    dirs = config_dir_index(primary_rows)
    cells = sorted({cell_key(r) for r in primary_rows if r["metric"] == "leo"})
    grouped = defaultdict(list)
    status_mismatch = defaultdict(int)
    raw_counts = defaultdict(lambda: [0, 0])
    for c in cells:
        layout, env, inter, cap = c
        leo = read_routes(dirs[(layout, env, inter, cap, "leo")] / "routes.csv")
        for comp in comparators:
            other = read_routes(dirs[(layout, env, inter, cap, comp)] / "routes.csv")
            div, joint, sm = route_pair_counts(leo, other)
            grouped[(comp, cap, inter)].append((div, joint))
            status_mismatch[(comp, cap, inter)] += sm
            raw_counts[(comp, cap, inter)][0] += int(div.sum())
            raw_counts[(comp, cap, inter)][1] += int(joint.sum())
    rows = []
    for (comp, cap, inter), items in sorted(grouped.items()):
        cell_frac = [d.sum() / j.sum() if j.sum() else np.nan for d, j in items]
        lo, hi = hierarchical_bootstrap_route(items, stable_seed("route", comp, cap, inter))
        divergent, joint = raw_counts[(comp, cap, inter)]
        rows.append({
            "comparator": comp, "relayCap": cap, "interference": inter,
            "n_cells": len(items), "joint_successful_transactions": joint,
            "divergent_route_transactions": divergent,
            "raw_transaction_divergence_fraction": divergent / joint if joint else float("nan"),
            "equal_cell_mean_divergence_fraction": float(np.nanmean(cell_frac)),
            "ci95_low": float(lo), "ci95_high": float(hi),
            "status_mismatch_transactions": status_mismatch[(comp, cap, inter)],
        })
    return rows
def summarize_eq17(primary_rows):
    sens = load_main_set("eq17_bounded_sensitivity")
    pidx = main_index(primary_rows)
    groups = defaultdict(lambda: defaultdict(list))
    for r in sens:
        c = (r["layout"], r["envFactor"], r["interference"], r["relayCap"])
        base = pidx[(r["layout"], r["envFactor"], r["interference"], r["relayCap"], "leo", r["run"])]
        for e in ENDPOINTS:
            groups[(e, r["relayCap"], r["interference"])][c].append(r[e] - base[e])
    rows = []
    for (e, cap, inter), cmap in sorted(groups.items()):
        arrays = [{"diff": np.asarray(v), "comp": np.zeros(len(v)), "leo": np.zeros(len(v))} for v in cmap.values()]
        effect = float(np.mean([a["diff"].mean() for a in arrays]))
        lo, hi = hierarchical_bootstrap_numeric(arrays, stable_seed("eq17", e, cap, inter))
        rows.append({"endpoint": e, "relayCap": cap, "interference": inter, "n_cells": len(arrays),
                     "effect_bounded_minus_literal": effect, "ci95_low": float(lo), "ci95_high": float(hi)})
    return rows


def summarize_nrf(primary_rows):
    sens = load_main_set("nrf52840_energy_sensitivity")
    pidx = main_index(primary_rows)
    groups = defaultdict(lambda: defaultdict(list))
    mismatch = defaultdict(int)
    for r in sens:
        c = (r["layout"], r["envFactor"], r["interference"], r["relayCap"])
        base = pidx[(r["layout"], r["envFactor"], r["interference"], r["relayCap"], r["metric"], r["run"])]
        groups[(r["metric"], r["relayCap"], r["interference"])][c].append(
            (r["totalEnergyMWs"] - base["totalEnergyMWs"], r["totalEnergyMWs"], base["totalEnergyMWs"])
        )
        if r["pingTimeouts"] != base["pingTimeouts"] or r["pingNoRoute"] != base["pingNoRoute"] or r["pingSent"] != base["pingSent"]:
            mismatch[(r["metric"], r["relayCap"], r["interference"])] += 1
    rows = []
    for key, cmap in sorted(groups.items()):
        metric, cap, inter = key
        arrays = []
        nrf_means = []
        base_means = []
        for vals in cmap.values():
            diff = np.asarray([x[0] for x in vals])
            arrays.append({"diff": diff, "comp": np.zeros(len(diff)), "leo": np.zeros(len(diff))})
            nrf_means.append(np.mean([x[1] for x in vals]))
            base_means.append(np.mean([x[2] for x in vals]))
        effect = float(np.mean([a["diff"].mean() for a in arrays]))
        base_mean = float(np.mean(base_means)); nrf_mean = float(np.mean(nrf_means))
        lo, hi = hierarchical_bootstrap_numeric(arrays, stable_seed("nrf", metric, cap, inter))
        rows.append({"metric": metric, "relayCap": cap, "interference": inter, "n_cells": len(arrays),
                     "base_energy_mean": base_mean, "nrf_energy_mean": nrf_mean,
                     "effect_nrf_minus_base": effect,
                     "energy_ratio_nrf_over_base": nrf_mean / base_mean if base_mean else float("nan"),
                     "ci95_low": float(lo), "ci95_high": float(hi),
                     "count_mismatch_runs": mismatch[key]})
    return rows, sens
def route_consistency_between_sets(primary_rows, sens_rows):
    pdirs = config_dir_index(primary_rows)
    sdirs = config_dir_index(sens_rows)
    status_mismatch = fp_mismatch_joint = hop_mismatch_joint = 0
    compared = joint_success = 0
    for skey, sdir in sorted(sdirs.items()):
        layout, env, inter, cap, metric = skey
        pdir = pdirs[(layout, env, inter, cap, metric)]
        a = read_routes(pdir / "routes.csv")
        b = read_routes(sdir / "routes.csv")
        for k, va in a.items():
            vb = b[k]; compared += 1
            if va[0] != vb[0]: status_mismatch += 1
            if va[0] == "success" and vb[0] == "success":
                joint_success += 1
                if va[1] != vb[1]: fp_mismatch_joint += 1
                if va[2] != vb[2]: hop_mismatch_joint += 1
    return {
        "route_transactions_compared": compared,
        "status_mismatch": status_mismatch,
        "joint_success": joint_success,
        "fingerprint_mismatch_among_joint_success": fp_mismatch_joint,
        "hopcount_mismatch_among_joint_success": hop_mismatch_joint,
    }


def summarize_ofat():
    rows = load_main_set("reconstruction_ofat_sensitivity")
    byid = defaultdict(list)
    dirs = {}
    for r in rows:
        byid[r["scenarioId"]].append(r)
        dirs[r["scenarioId"]] = Path(r["_dir"])
    ref = sorted(byid["reference"], key=lambda r: r["run"])
    ref_by_run = {r["run"]: r for r in ref}
    ref_routes = read_routes(dirs["reference"] / "routes.csv")
    out = []
    for sid in sorted(byid):
        if sid == "reference":
            continue
        vals = sorted(byid[sid], key=lambda r: r["run"])
        d_energy = np.array([r["totalEnergyMWs"] - ref_by_run[r["run"]]["totalEnergyMWs"] for r in vals])
        d_to = np.array([r["pingTimeouts"] - ref_by_run[r["run"]]["pingTimeouts"] for r in vals])
        d_nr = np.array([r["pingNoRoute"] - ref_by_run[r["run"]]["pingNoRoute"] for r in vals])
        base_energy = np.mean([ref_by_run[r["run"]]["totalEnergyMWs"] for r in vals])
        route = read_routes(dirs[sid] / "routes.csv")
        div, joint, sm = route_pair_counts(route, ref_routes)
        out.append({
            "scenarioId": sid,
            "energy_effect_abs": float(d_energy.mean()),
            "energy_effect_pct_vs_reference": float(100*d_energy.mean()/base_energy),
            "pingTimeouts_effect": float(d_to.mean()),
            "pingNoRoute_effect": float(d_nr.mean()),
            "joint_successful_routes": int(joint.sum()),
            "route_divergence_fraction": float(div.sum()/joint.sum()) if joint.sum() else float("nan"),
            "route_status_mismatch_transactions": sm,
        })
    return out
def summarize_mobility():
    rows = load_main_set("mobility_extension")
    idx = {
        (r["mobility"], r["layout"], r["envFactor"], r["metric"], r["run"]): r
        for r in rows
    }
    cells = sorted({(r["mobility"], r["layout"], r["envFactor"]) for r in rows if r["metric"] == "leo"})
    out = []
    for mode in ["walk", "waypoint"]:
        for comp in PRIMARY_COMPARATORS:
            for endpoint in ENDPOINTS:
                arrays = []
                for c in cells:
                    if c[0] != mode:
                        continue
                    _, layout, env = c
                    diff = []
                    compv = []
                    leov = []
                    for run in range(20):
                        a = idx[(mode, layout, env, "leo", run)][endpoint]
                        b = idx[(mode, layout, env, comp, run)][endpoint]
                        diff.append(a-b); compv.append(b); leov.append(a)
                    arrays.append({"diff":np.asarray(diff),"comp":np.asarray(compv),"leo":np.asarray(leov)})
                comp_mean=float(np.mean([x["comp"].mean() for x in arrays]))
                leo_mean=float(np.mean([x["leo"].mean() for x in arrays]))
                eff=float(np.mean([x["diff"].mean() for x in arrays]))
                lo,hi=hierarchical_bootstrap_numeric(arrays, stable_seed("mob",mode,comp,endpoint))
                out.append({"mobility":mode,"comparator":comp,"endpoint":endpoint,"n_cells":len(arrays),
                            "comparator_mean":comp_mean,"leo_mean":leo_mean,
                            "effect_abs_leo_minus_comparator":eff,
                            "effect_pct_vs_comparator":100*eff/comp_mean if comp_mean else float("nan"),
                            "ci95_low":float(lo),"ci95_high":float(hi)})
    return out



def summarize_mobility_routes():
    rows = load_main_set("mobility_extension")
    dirs = config_dir_index(rows, include_mobility=True)
    cells = sorted({(r["mobility"], r["layout"], r["envFactor"]) for r in rows if r["metric"] == "leo"})
    grouped = defaultdict(list)
    mismatch = defaultdict(int)
    raw = defaultdict(lambda: [0, 0])
    for mode, layout, env in cells:
        leo = read_routes(dirs[(mode, layout, env, 0, 3, "leo")] / "routes.csv")
        for comp in PRIMARY_COMPARATORS:
            other = read_routes(dirs[(mode, layout, env, 0, 3, comp)] / "routes.csv")
            div, joint, sm = route_pair_counts(leo, other)
            grouped[(mode, comp)].append((div, joint))
            mismatch[(mode, comp)] += sm
            raw[(mode, comp)][0] += int(div.sum())
            raw[(mode, comp)][1] += int(joint.sum())
    out = []
    for (mode, comp), items in sorted(grouped.items()):
        fracs = [d.sum()/j.sum() if j.sum() else np.nan for d,j in items]
        lo, hi = hierarchical_bootstrap_route(items, stable_seed("mobroute", mode, comp))
        divergent, joint = raw[(mode, comp)]
        out.append({
            "mobility": mode, "comparator": comp, "n_cells": len(items),
            "joint_successful_transactions": joint,
            "divergent_route_transactions": divergent,
            "raw_transaction_divergence_fraction": divergent/joint if joint else float("nan"),
            "equal_cell_mean_divergence_fraction": float(np.nanmean(fracs)),
            "ci95_low": float(lo), "ci95_high": float(hi),
            "status_mismatch_transactions": mismatch[(mode, comp)],
        })
    return out

def main():
    integrity = json.load(open(RESULTS / "R5_INTEGRITY.json"))
    if integrity.get("status") != "PASS" or integrity.get("configurations") != 617:
        raise SystemExit("R5 integrity gate not PASS")

    primary = load_main_set("primary_static_source_constrained")
    primary_summary, cell_effects = summarize_primary_numeric(primary, PRIMARY_COMPARATORS, True)
    diagnostic_summary, _ = summarize_primary_numeric(primary, ["lqi-literal"], False)
    route_summary = summarize_primary_routes(primary, PRIMARY_COMPARATORS)
    diagnostic_route = summarize_primary_routes(primary, ["lqi-literal"])

    eq17 = summarize_eq17(primary)
    eq17_rows = load_main_set("eq17_bounded_sensitivity")
    eq17_route_consistency = route_consistency_between_sets(primary, eq17_rows)
    nrf, nrf_rows = summarize_nrf(primary)
    nrf_route_consistency = route_consistency_between_sets(primary, nrf_rows)
    ofat = summarize_ofat()
    mobility = summarize_mobility()
    mobility_route = summarize_mobility_routes()

    write_csv(OUT/"primary_summary.csv", primary_summary,
              ["comparator","endpoint","relayCap","interference","n_cells","runs_per_cell",
               "comparator_mean","leo_mean","effect_abs_leo_minus_comparator","effect_pct_vs_comparator",
               "ci95_low","ci95_high","p_raw","p_holm","significant_holm_0p05","permutation_method","permutations"])
    write_csv(OUT/"primary_cell_effects.csv", cell_effects,
              ["comparator","endpoint","layout","envFactor","interference","relayCap","n_runs",
               "comparator_mean","leo_mean","effect_abs_leo_minus_comparator"])
    write_csv(OUT/"primary_route_divergence.csv", route_summary,
              ["comparator","relayCap","interference","n_cells","joint_successful_transactions",
               "divergent_route_transactions","raw_transaction_divergence_fraction",
               "equal_cell_mean_divergence_fraction","ci95_low","ci95_high","status_mismatch_transactions"])
    write_csv(OUT/"diagnostic_lqi_literal.csv", diagnostic_summary,
              ["comparator","endpoint","relayCap","interference","n_cells","runs_per_cell",
               "comparator_mean","leo_mean","effect_abs_leo_minus_comparator","effect_pct_vs_comparator",
               "ci95_low","ci95_high"])
    write_csv(OUT/"diagnostic_lqi_literal_route.csv", diagnostic_route,
              ["comparator","relayCap","interference","n_cells","joint_successful_transactions",
               "divergent_route_transactions","raw_transaction_divergence_fraction",
               "equal_cell_mean_divergence_fraction","ci95_low","ci95_high","status_mismatch_transactions"])
    write_csv(OUT/"eq17_bounded_sensitivity.csv", eq17,
              ["endpoint","relayCap","interference","n_cells","effect_bounded_minus_literal","ci95_low","ci95_high"])
    (OUT/"eq17_route_consistency.json").write_text(json.dumps(eq17_route_consistency,indent=2,sort_keys=True)+"\n")
    write_csv(OUT/"nrf52840_energy_sensitivity.csv", nrf,
              ["metric","relayCap","interference","n_cells","base_energy_mean","nrf_energy_mean",
               "effect_nrf_minus_base","energy_ratio_nrf_over_base","ci95_low","ci95_high","count_mismatch_runs"])
    (OUT/"nrf52840_route_consistency.json").write_text(json.dumps(nrf_route_consistency,indent=2,sort_keys=True)+"\n")
    write_csv(OUT/"ofat_sensitivity.csv", ofat,
              ["scenarioId","energy_effect_abs","energy_effect_pct_vs_reference","pingTimeouts_effect",
               "pingNoRoute_effect","joint_successful_routes","route_divergence_fraction",
               "route_status_mismatch_transactions"])
    write_csv(OUT/"mobility_exploratory.csv", mobility,
              ["mobility","comparator","endpoint","n_cells","comparator_mean","leo_mean",
               "effect_abs_leo_minus_comparator","effect_pct_vs_comparator","ci95_low","ci95_high"])
    write_csv(OUT/"mobility_route_divergence.csv", mobility_route,
              ["mobility","comparator","n_cells","joint_successful_transactions",
               "divergent_route_transactions","raw_transaction_divergence_fraction",
               "equal_cell_mean_divergence_fraction","ci95_low","ci95_high","status_mismatch_transactions"])

    metadata = {
        "status":"PASS",
        "r5_integrity_sha256":sha256(RESULTS/"R5_INTEGRITY.json"),
        "r6_sap_sha256":sha256(REPO/"experiments/v1.1.0/R6_STATISTICAL_ANALYSIS_PLAN.md"),
        "analysis_script_sha256":sha256(Path(__file__)),
        "bootstrap_replicates":BOOT_REPS,
        "permutation_replicates_monte_carlo":PERM_REPS,
        "base_seed":BASE_SEED,
        "primary_comparators":PRIMARY_COMPARATORS,
        "primary_endpoints":ENDPOINTS + ["routeFingerprint divergence"],
        "outputs": sorted(p.name for p in OUT.iterdir() if p.is_file()),
    }
    (OUT/"R6_ANALYSIS_METADATA.json").write_text(json.dumps(metadata,indent=2,sort_keys=True)+"\n")
    print("R6_ANALYSIS=PASS")
    print("PRIMARY_SUMMARY_ROWS",len(primary_summary))
    print("PRIMARY_CELL_EFFECT_ROWS",len(cell_effects))
    print("PRIMARY_ROUTE_ROWS",len(route_summary))
    print("NRF_ROUTE_CONSISTENCY",json.dumps(nrf_route_consistency,sort_keys=True))
    print("OUTPUT_DIR",OUT)


if __name__ == "__main__":
    main()
