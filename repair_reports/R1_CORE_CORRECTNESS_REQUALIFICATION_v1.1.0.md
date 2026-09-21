# R1 — Core Correctness Requalification

## Program
**LEO v1.1.0 — Repair & Requalification**

## Gate
`R1 — Core Correctness Requalification`

## Status
`PASS — R1 CLOSED`

---

# 1. Baselines

Historical immutable release:

```text
v1.0.0 tag   = 7790a1522f83278aa9e1d7c05634ec23236ba387
v1.0.0 tree  = 2b8f782100ebc8b091e4a2afca4197559528be33
```

Repair branch base:

```text
bec4802891f19ccf33c978ead408677a10df3629
message: Update Concept DOI
```

R1 code head:

```text
fdcda51e96ec1876bea2b915b433dc199bae9840
tree: 8b216623f20efcb5635b2683bc786f45ae7cdc1e
branch: repair/v1.1.0
```

The post-v1.0.0 base difference is release metadata only; the v1.0.0 scientific baseline remains immutable.

---

# 2. Live qualification environment

```text
ns-3 tag: ns-3.48
ns-3 commit: d2add90b452d600cfb4859baed8e9ea633519447
compiler: g++ 13.3.0
CMake: 3.28.3
tests: enabled
assertions: enabled
```

A real ns-3.48 module/test build was performed.

This closes the earlier "no live clean build evidence" gap for the repaired R1 module path. It does not yet constitute the full CI/version matrix required by R3/R7.

---

# 3. R1 repair commits

```text
dc42a38  test: add R1 routing regression suite
643cb77  test: split R1 regression cases into independent suites
064cdf3  test: make stale discovery probe non-throwing
6adc874  fix: make routing transactions identity-safe
9e2f70f  test: add R1 lifecycle and ARQ regression cases
8433898  test: include RNG seed manager for context regression
fdcda51  fix: close R1 lifecycle and ARQ defects
```

R1 diff from repair-branch base:

```text
8 files changed
769 insertions
28 deletions
```

No v1.0.0 tag/data file was modified.

---

# 4. Tests-first evidence

## T-R1-001 — Eq.12–13 linkUsable enforcement

Before repair:

```text
FAIL
linkUsable=false
route still installed
```

After repair:

```text
PASS
```

Closure:
- G2-CORE-001
- G15-CLAIM-001 implementation contradiction

Repair:
`LinkMetricTo()` now propagates `LinkMetricResult`; unusable links are rejected before route mutation on discovery and reply paths.

---

## T-R1-002 — failed discovery timeout/restart

Before repair:

```text
FAIL
pending discovery remained present after 1.1 s
```

After repair:

```text
PASS
```

Closure:
- G5-DISC-001

Repair:
each local discovery has a liveness timeout tied to exact `(targetId,floodId)`.

Important qualification:
`m_discoveryTimeoutS=1.0` is a reconstruction liveness parameter, not a source-authoritative value. It must be exposed/frozen/swept by R4 before production experiments.

---

## T-R1-003 — stale discovery reply rejection

Before repair:

```text
FAIL
stale reply cleared the newer pending discovery
current flood id disappeared
stale reply installed a forward route
```

After repair:

```text
PASS
```

Closure:
- G5-DISC-002

Repair:
transaction identity is checked before any routing-table mutation.

---

## T-R1-004 — stale ping reply timeout isolation

Before repair:

```text
FAIL
unmatched old reply cancelled the active timeout
```

After repair:

```text
PASS
```

Closure:
- G5-PING-003

Repair:
ping timeout ownership is keyed by exact `(targetId,seq)`.

---

## T-R1-005 — matching ping reply control

Before repair:

```text
PASS
```

After repair:

```text
PASS
```

This is the positive control showing that correct matching replies still cancel their own timeout.

---

## T-R1-006 — ARQ exhaustion route invalidation

Before repair:

```text
FAIL
routes through failed nextHop remained valid
```

After repair:

```text
PASS
```

Closure:
- G5-ROUTE-005 policy defect

R1 policy:
final per-hop ARQ exhaustion invalidates every currently valid route whose `nextHopId` equals the failed neighbor. Unrelated routes remain valid.

This is a deterministic reconstruction policy; no claim is made that this exact invalidation rule is specified by the original paper.

---

## T-R1-007 — frame safety counter per-run reset

Before repair:

```text
FAIL
counter after Simulator::Destroy() = 1234
```

After repair:

```text
PASS
counter after Simulator::Destroy() = 0
```

Closure:
- G3-STATE-002
- G13-LIMIT-005 cross-run component

Repair:
`Simulator::ScheduleDestroy()` resets the shared frame counter at the authoritative run boundary.

---

## T-R1-008 — channel/device lifecycle cleanup

Before repair:

```text
FAIL
channel GetNDevices() remained 1 after Dispose()
device retained channel back-reference
```

After repair:

```text
PASS
```

Closure:
- G3-LIFE-001 core ownership cycle

Repair:
- `WsnChannel::DoDispose()` clears device back-references and owned-device vector.
- `WsnNetDevice::DoDispose()` clears receive callback and channel pointer.
- `WsnRoutingApp::DoDispose()` cancels/clears transaction state and removes the receive callback/device reference.

---

## T-R1-009 — receiver node execution context

Before repair:

```text
20 deliveries
20 wrong contexts
```

After repair:

```text
PASS
all delivered callbacks execute under receiver node context
```

Closure:
- G1-EVENT-002
- G4-CTX-001 for channel delivery

Repair:
`Simulator::ScheduleNow(...)` was replaced with `Simulator::ScheduleWithContext(receiverNodeId, Seconds(0), ...)`.

---

# 5. Full regression result

Authoritative R1 suite result:

```text
leo-r1-link-usable                  RC=0
leo-r1-discovery-timeout            RC=0
leo-r1-stale-discovery-reply        RC=0
leo-r1-stale-ping-reply             RC=0
leo-r1-matching-ping-reply          RC=0
leo-r1-arq-route-invalidation       RC=0
leo-r1-frame-counter-reset          RC=0
leo-r1-lifecycle-cleanup            RC=0
leo-r1-delivery-context             RC=0

TOTAL = 9/9 PASS
FINAL_RC = 0
```

---

# 6. Executable/oracle qualification

The real C++ equation checker compiled and executed against the repaired module.

It emitted the expected 13 values, including:

```text
eq1_pl_0.95=1.000000
eq1_pl_0.5=7.000000
eq3_rssi_-80=12.000000
eq2_lqi_12=1.000000
eq3_rssi_-50=42.000000
eq2_lqi_42=1.312500
eq17_delta15=4.000000
eq16_neither_known=4.000000
eq14_r0_ptxmax=7.309573
```

Independent comparison:

```text
Python equation_oracle.py --emit-values
vs
C++ leo-equation-check

ORACLE_DIFF = PASS
```

R1 did not change the established metric arithmetic oracle.

---

# 7. Driver smoke qualification

A non-scientific smoke run was executed:

```text
layout=line
nNodes=2
spacingM=5
envFactor=2.0
metric=hopcount
runs=2
seedBase=777
```

Result:

```text
SMOKE_RC=0
CSV lines=3
1 header + 2 data rows
```

This run is **qualification only**.

It is not part of any scientific dataset and must not be used for performance/energy conclusions.

---

# 8. Findings explicitly NOT closed by R1

R1 intentionally does not address the R2/R3/R4 items below.

## R2 — Radio / Physical / Energy
- G7-ATPC-001 TX-power discreteness tied to energy mode
- G7-ATPC-002 realized TX vs metric TX inconsistency
- G7-RX-004 failed-RX energy
- G7-RX-005 ACK-listening energy
- G6-LQI-003 interference/effective-SNR consistency
- interference-model/source-fidelity decision
- Eq.4 / Eq.17 reconstruction policy
- explicit/calibrated PHY/MAC timing model

## R3 — RNG / Robustness / Runtime
- explicit RNG stream assignment/common-random-number design
- adversarial CLI validation
- NaN/Inf guards
- output-path collision
- long-run stability/sanitizers
- broader Channel API engineering issues

## R4 — Experiment freeze
- exact source-study scenario subset
- sensitivity grid
- discovery timeout value and sensitivity
- manifest/config identity
- relay cap runtime recording

---

# 9. Data/regeneration consequence

R1 changes real routing behavior.

Therefore:

```text
CURRENT v1.0.0 RAW DATA = HISTORICAL ONLY
PATCHING OLD CSVs = FORBIDDEN
FULL RAW REGENERATION LATER = REQUIRED
```

No production scientific data were regenerated during R1.

---

# 10. Remote/push status

An attempt to create the repair branch through the GitHub integration returned:

```text
403 Resource not accessible by integration
```

Therefore R1 was executed on the authorized local clone:

```text
/home/pharmaco/projects/LEO_v1.1.0_Repair
branch: repair/v1.1.0
```

No claim is made that these commits are currently pushed to GitHub.

---

# 11. R1 gate decision

```text
R1 = PASS
R1 = CLOSED
R2 = OPEN
SCIENTIFIC_RERUN = CLOSED
```

R1 exit criteria are satisfied:

- tests-first defects reproduced;
- core repairs implemented;
- 9/9 regression tests PASS with RC=0;
- real ns-3.48 build path verified;
- C++/Python equation oracle agreement preserved;
- full driver smoke execution passes;
- no historical release/data mutation occurred.

---

# 12. Next allowed action

Open:

```text
R2 — Radio / Physical / Energy Requalification
```

First R2 test block should address a single authoritative `realizedTxPowerDbm` and decouple physical TX-power availability from energy-accounting mode before modifying energy equations or running any scientific campaign.
