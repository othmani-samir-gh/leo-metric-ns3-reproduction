#!/usr/bin/env bash
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
NS3="${NS3_ROOT:-/home/pharmaco/projects/ns-3.48-leo-r1}"
OUT="$REPO/build/r5"
PROV="$REPO/results/v1.1.0/provenance"
EXPECTED_MANIFEST="f36d0510e3dbb0d3c9adffa565c74ad2c5026b77fa1cf04b411ba46f49f4c520"
EXPECTED_PLAN="fb6d360f6f1f88ea50e59b91d2ae147e0c22c2c3e9ff195212e8fe89c932428e"

mkdir -p "$OUT" "$PROV"

cd "$REPO"
test "$(sha256sum experiments/v1.1.0/manifest.json | awk '{print $1}')" = "$EXPECTED_MANIFEST"
test "$(sha256sum experiments/v1.1.0/frozen_plan.jsonl | awk '{print $1}')" = "$EXPECTED_PLAN"
test -z "$(git status --porcelain --untracked-files=no)"

rm -rf "$NS3/contrib/leo-wsn"
cp -a "$REPO/contrib/leo-wsn" "$NS3/contrib/leo-wsn"

cd "$NS3"
./ns3 configure --enable-tests --disable-examples >/tmp/leo-r5-configure.log
cmake --build cmake-cache --target leo-wsn -j3 >/tmp/leo-r5-module-build.log 2>&1

COMMON="-std=c++23 -O2 -I$NS3/build/include -L$NS3/build/lib -Wl,-rpath,$NS3/build/lib"
LIBS="-Wl,--no-as-needed -lns3.48-leo-wsn-debug -Wl,--as-needed -lns3.48-applications-debug -lns3.48-energy-debug -lns3.48-mobility-debug -lns3.48-network-debug -lns3.48-core-debug -lstdc++_libbacktrace"
g++ $COMMON "$REPO/scratch/leo-topologies.cc" $LIBS -o "$OUT/leo-topologies"

cd "$REPO"
python3 - "$REPO" "$NS3" "$EXPECTED_MANIFEST" "$EXPECTED_PLAN" <<'PY'
import hashlib, json, pathlib, subprocess, sys
repo=pathlib.Path(sys.argv[1])
ns3=pathlib.Path(sys.argv[2])
manifest_sha=sys.argv[3]
plan_sha=sys.argv[4]
binary=repo/"build/r5/leo-topologies"
lib=ns3/"build/lib/libns3.48-leo-wsn-debug.so"
def sha(p): return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
receipt={
 "schema_version":"1.0",
 "stage":"R5_BUILD",
 "source_commit": subprocess.check_output(["git","rev-parse","HEAD"],cwd=repo,text=True).strip(),
 "source_tree": subprocess.check_output(["git","rev-parse","HEAD^{tree}"],cwd=repo,text=True).strip(),
 "ns3_commit": subprocess.check_output(["git","rev-parse","HEAD"],cwd=ns3,text=True).strip(),
 "compiler": subprocess.check_output(["g++","--version"],text=True).splitlines()[0],
 "cmake": subprocess.check_output(["cmake","--version"],text=True).splitlines()[0],
 "manifest_sha256":manifest_sha,
 "plan_sha256":plan_sha,
 "binary_path": str(binary),
 "binary_sha256": sha(binary),
 "leo_wsn_library_sha256": sha(lib),
}
out=repo/"results/v1.1.0/provenance/build_receipt.json"
out.write_text(json.dumps(receipt,indent=2,sort_keys=True)+"\n")
print(json.dumps(receipt,indent=2,sort_keys=True))
PY
