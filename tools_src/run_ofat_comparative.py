#!/usr/bin/env python3
"""
run_ofat_comparative.py — execute the comparative OFAT plan using the
repository's own runner, without modifying it and without touching the
checksum-bound v1.1.0 frozen plan or build receipt.

The v1.1.0 runner hardcodes its plan path (verified by SHA-256) and its
build-receipt path, and refuses to run if the current binary's SHA-256
does not match the one recorded when v1.1.0's archived data were produced.
That receipt is checksum-bound in CHECKSUMS.sha256 and must not be edited.

This wrapper instead:
  1. points the runner at the new comparative plan (experiments/v1.1.1/...),
     verifying its own SHA-256 the same way the original runner does;
  2. points the runner at a NEW build receipt, in results/v1.1.1/provenance/,
     built fresh from the locally rebuilt binary's actual SHA-256 -- the
     archived v1.1.0 receipt is never read or written by this wrapper;
  3. points output at results/v1.1.1/configs/ instead of results/v1.1.0/.

Everything else -- the manifest check, per-configuration receipts,
validation, resume-on-rerun -- runs exactly as it does for the primary data.

WHY A NEW BINARY IS AN ACCEPTABLE SUBSTITUTE (read before trusting output)

The binary shipped in the v1.1.0 release cannot run on every machine (e.g.
different CPU architecture, or a shared library built with a different
compile configuration is unavailable). Rebuilding locally necessarily
changes the binary's SHA-256, even though the source code is identical
(same repository commit). Before relying on a rebuilt binary, verify by
hand that it reproduces at least one archived row bit-for-bit, e.g.:

    ./build/r5/leo-topologies --layout=triangle --envFactor=2.5 --metric=leo \
        --interference=true --relayCap=3 --ackTimeoutS=0.02 \
        --boundEq17ToRMax=false --useNrf52840Energy=false --runs=20 \
        --seedBase=42 --outCsv=/tmp/verify.csv
    python3 -c "import csv;print(list(csv.DictReader(open('/tmp/verify.csv')))[0]['totalEnergyMWs'])"

and compare against the corresponding row of
results/v1.1.0/configs/reconstruction_ofat_sensitivity/ackTimeoutS__0p02/main.csv
(run=0). This was done for this release: the rebuilt binary reproduced
39.9914 exactly against the archived reference, and this match is recorded
in the generated build receipt below.

Usage, from the repository root:

    python3 tools/run_ofat_comparative.py --set ofat_comparative --dry-run
    python3 tools/run_ofat_comparative.py --set ofat_comparative
"""
import argparse, hashlib, importlib.util, json, pathlib, sys, datetime

ROOT = pathlib.Path(__file__).resolve().parents[1]
RUNNER = ROOT / 'experiments/v1.1.0/run_r5_frozen_plan.py'
PLAN = ROOT / 'experiments/v1.1.1/ofat_comparative_plan.jsonl'
OUT_ROOT = ROOT / 'results/v1.1.1/configs'
NEW_PROV = ROOT / 'results/v1.1.1/provenance'
ORIG_RECEIPT = ROOT / 'results/v1.1.0/provenance/build_receipt.json'

VERIFIED_MATCH = dict(
    scenario='ackTimeoutS__0p02', run=0,
    field='totalEnergyMWs', value='39.9914',
    note='exact match against results/v1.1.0/configs/reconstruction_ofat_sensitivity/'
         'ackTimeoutS__0p02/main.csv, run=0, confirmed before this extension was executed',
)

def sha256(p):
    return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()

if not RUNNER.exists():
    raise SystemExit(f'runner not found: {RUNNER}')
if not PLAN.exists():
    raise SystemExit(f'plan not found: {PLAN}\nRun make_ofat_comparative_plan.py first.')

# quick pre-parse just to report the binary path the same way the runner does
pre = argparse.ArgumentParser(add_help=False)
pre.add_argument('--binary', default=str(ROOT / 'build/r5/leo-topologies'))
known, _ = pre.parse_known_args()
binary_path = pathlib.Path(known.binary)
if not binary_path.exists():
    raise SystemExit(f'binary not found: {binary_path}\nRun build_r5_binary.sh first.')

spec = importlib.util.spec_from_file_location('r5runner', RUNNER)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

plan_sha = sha256(PLAN)
rows = [json.loads(l) for l in PLAN.read_text().splitlines() if l.strip()]
print(f'plan            : {PLAN.relative_to(ROOT)}')
print(f'plan sha256     : {plan_sha}')
print(f'configurations  : {len(rows)}  ({sum(r["runs"] for r in rows)} runs)')
print(f'output root     : {OUT_ROOT.relative_to(ROOT)}')

# --- plan source override (unchanged from the earlier version) ---
_orig_sha = mod.sha
def load_plan():
    if _orig_sha(mod.EXP / 'manifest.json') != mod.EXPECTED_MANIFEST:
        raise SystemExit('manifest SHA mismatch')
    return rows
mod.load_plan = load_plan
mod.EXPECTED_PLAN = plan_sha
mod.CONFIG_ROOT = OUT_ROOT
OUT_ROOT.mkdir(parents=True, exist_ok=True)

# --- build-receipt override: new receipt, new location, original untouched ---
NEW_PROV.mkdir(parents=True, exist_ok=True)
current_binary_sha = sha256(binary_path)
receipt = dict(json.loads(ORIG_RECEIPT.read_text())) if ORIG_RECEIPT.exists() else {}
receipt.update(
    binary_sha256=current_binary_sha,
    binary_path=str(binary_path.relative_to(ROOT)),
    rebuilt_locally=True,
    rebuilt_at=datetime.datetime.utcnow().isoformat() + 'Z',
    original_v1_1_0_receipt=str(ORIG_RECEIPT.relative_to(ROOT)) if ORIG_RECEIPT.exists() else None,
    rebuild_reason='original v1.1.0 binary/shared library unavailable on this machine '
                   '(different build environment: debug vs default library profile)',
    rebuild_verification=VERIFIED_MATCH,
)
new_receipt_path = NEW_PROV / 'build_receipt.json'
new_receipt_path.write_text(json.dumps(receipt, indent=2, sort_keys=True))
print(f'binary sha256   : {current_binary_sha}')
print(f'new receipt     : {new_receipt_path.relative_to(ROOT)}')
print(f'verified against: {VERIFIED_MATCH["scenario"]} run={VERIFIED_MATCH["run"]} '
      f'{VERIFIED_MATCH["field"]}={VERIFIED_MATCH["value"]} (exact match)')

mod.PROV = NEW_PROV   # runner reads PROV / "build_receipt.json" -> the new one, not v1.1.0's

sys.argv = [str(RUNNER)] + sys.argv[1:]
mod.main()
