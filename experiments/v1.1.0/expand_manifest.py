#!/usr/bin/env python3
import hashlib
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent
M = json.loads((ROOT / "manifest.json").read_text())
SHA = hashlib.sha256((ROOT / "manifest.json").read_bytes()).hexdigest()
FIX = M["fixed_reconstruction_parameters"]
RUNS = M["rng"]["runs_per_configuration"]
SEED = M["rng"]["seed_base"]

def fmt(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    return str(v)

def base_params(set_id, scenario_id, **overrides):
    p = {
        "experimentId": M["experiment_id"],
        "scenarioSet": set_id,
        "scenarioId": scenario_id,
        "manifestSha256": SHA,
        "runs": RUNS,
        "seedBase": SEED,
        **FIX,
    }
    p.update(overrides)
    # Fixed implementation mode currently exposed by driver but not as a CLI enum.
    p.pop("atpcMode", None)
    return p

def interference_states(layout, policy):
    if policy in ("standard_plus_triangle_interference",):
        return [False, True] if layout == "triangle" else [False]
    if isinstance(policy, dict):
        extra = set(policy.get("additional_interference_condition_for_layouts", []))
        return [False, True] if layout in extra else [False]
    raise ValueError(policy)

def emit(plan, set_id, scenario_id, params):
    raw = f"results/v1.1.0/raw/{set_id}.csv"
    routes = f"results/v1.1.0/routes/{set_id}_routes.csv"
    params = dict(params)
    params["outCsv"] = raw
    params["routeCsv"] = routes
    argv = [f"--{k}={fmt(v)}" for k, v in params.items()]
    plan.append({
        "scenarioSet": set_id,
        "scenarioId": scenario_id,
        "manifestSha256": SHA,
        "runs": RUNS,
        "rawCsv": raw,
        "routeCsv": routes,
        "argv": argv,
    })

plan = []

# Primary
p = M["primary"]
for layout in p["layouts"]:
    for env in p["envFactors"]:
        for intr in interference_states(layout, p["interference_policy"]):
            for cap in p["relayCaps"]:
                for metric in p["metrics"]:
                    sid = f"{layout}__e{env:g}__i{int(intr)}__cap{cap}__{metric}"
                    params = base_params(
                        p["id"], sid,
                        layout=layout, envFactor=env, metric=metric,
                        interference=intr,
                        interferenceDb=p["interference_policy"]["interferenceDb"],
                        relayCap=cap, mobility="none",
                        boundEq17ToRMax=p["boundEq17ToRMax"],
                        useNrf52840Energy=p["useNrf52840Energy"],
                    )
                    emit(plan, p["id"], sid, params)

byid = {x["id"]: x for x in M["sensitivity_sets"]}

# Eq17 bounded sensitivity
s = byid["eq17_bounded_sensitivity"]
for layout in s["layouts"]:
    for env in s["envFactors"]:
        for intr in interference_states(layout, s["interference_policy"]):
            for cap in s["relayCaps"]:
                for metric in s["metrics"]:
                    sid = f"{layout}__e{env:g}__i{int(intr)}__cap{cap}__{metric}__bounded"
                    params = base_params(
                        s["id"], sid, layout=layout, envFactor=env, metric=metric,
                        interference=intr, interferenceDb=15.0, relayCap=cap,
                        mobility="none", **s["overrides"]
                    )
                    emit(plan, s["id"], sid, params)

# nRF52840 energy sensitivity
s = byid["nrf52840_energy_sensitivity"]
for layout in s["layouts"]:
    for env in s["envFactors"]:
        for intr in interference_states(layout, s["interference_policy"]):
            for cap in s["relayCaps"]:
                for metric in s["metrics"]:
                    sid = f"{layout}__e{env:g}__i{int(intr)}__cap{cap}__{metric}__nrf"
                    params = base_params(
                        s["id"], sid, layout=layout, envFactor=env, metric=metric,
                        interference=intr, interferenceDb=15.0, relayCap=cap,
                        mobility="none", **s["overrides"]
                    )
                    emit(plan, s["id"], sid, params)

# OFAT sensitivity: reference once + two non-reference alternatives for each factor.
s = byid["reconstruction_ofat_sensitivity"]
ref = dict(s["reference"])
ref_params = base_params(
    s["id"], "reference",
    layout=ref["layout"], envFactor=ref["envFactor"], metric=ref["metric"],
    interference=ref["interference"], relayCap=ref["relayCap"],
    mobility=ref["mobility"], boundEq17ToRMax=ref["boundEq17ToRMax"],
    useNrf52840Energy=ref["useNrf52840Energy"],
)
emit(plan, s["id"], "reference", ref_params)
for factor, values in s["factors"].items():
    reference_value = FIX.get(factor, 15.0 if factor == "interferenceDb" else None)
    for value in values:
        if value == reference_value:
            continue
        sid = f"{factor}__{fmt(value).replace('.', 'p').replace('-', 'm')}"
        params = dict(ref_params)
        params["scenarioId"] = sid
        params[factor] = value
        emit(plan, s["id"], sid, params)

# Mobility extension
s = byid["mobility_extension"]
for mv in s["mobility_variants"]:
    for layout in s["layouts"]:
        for env in s["envFactors"]:
            for metric in s["metrics"]:
                mode = mv["mobility"]
                sid = f"{mode}__{layout}__e{env:g}__{metric}"
                params = base_params(
                    s["id"], sid, layout=layout, envFactor=env, metric=metric,
                    interference=s["interference"], relayCap=s["relayCaps"][0],
                    **s["overrides"], **mv
                )
                emit(plan, s["id"], sid, params)

out = ROOT / "frozen_plan.jsonl"
with out.open("w", encoding="utf-8") as f:
    for row in plan:
        f.write(json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n")

print("PLAN_CONFIGS", len(plan))
print("PLAN_SIMULATION_ROWS", sum(x["runs"] for x in plan))
print("PLAN_SHA256", hashlib.sha256(out.read_bytes()).hexdigest())
counts = {}
for x in plan:
    counts[x["scenarioSet"]] = counts.get(x["scenarioSet"], 0) + 1
print("PLAN_CONFIGS_BY_SET", json.dumps(counts, sort_keys=True))
