# R3 — RNG / Robustness / Runtime Requalification

## Program
**LEO v1.1.0 — Repair & Requalification**

## Gate
`R3 — RNG / Robustness / Runtime Requalification`

## Status
`PASS — R3 CLOSED`

---

# 1. Baselines

Historical immutable release:

```text
v1.0.0 tag   = 7790a1522f83278aa9e1d7c05634ec23236ba387
v1.0.0 tree  = 2b8f782100ebc8b091e4a2afca4197559528be33
```

R2 authoritative closeout commit:

```text
32109e2  docs: close R2 radio physical energy requalification
```

R3 code/documentation head before this closeout report:

```text
branch = repair/v1.1.0
head   = b72e375a3b3167bed84d04c575e62cbbb87eeaef
tree   = 14ddc19ec75667ace2dbeb46c8558abbf1cf81f0
```

No v1.0.0 tag or historical raw/derived file was modified.

---

# 2. Live qualification environment

```text
ns-3 series/checkout = ns-3.48
ns-3 commit          = d2add90b452d600cfb4859baed8e9ea633519447
compiler             = g++ 13.3.0
CMake                = 3.28.3
tests                = enabled
assertions           = enabled
```

R3 unit/regression tests and replay qualification were executed against the repaired module.

---

# 3. R3 commits

```text
2f9b7ea  test: require explicit channel RNG stream identity
6db698c  test: make RNG sequence assertion portable
eba404b  fix: bind deterministic RNG stream identities
6b331bb  fix: fail fast on unsafe simulation inputs
71866e7  test: expose misleading ns3 Channel contract
c1c3bdc  fix: replace misleading ns3 Channel inheritance
f5959c9  fix: break device-channel reference cycle
c17b88c  fix: fail closed on missing channel mobility
b72e375  docs: document R3 RNG and runtime repairs
```

R3 diff from R2 closeout:

```text
8 files changed
339 insertions
32 deletions
```

---

# 4. RNG stream identity repair

## Prior defect

v1.0.0/R2 used:

```text
RngSeedManager::SetSeed(seedBase)
RngSeedManager::SetRun(run + 1)
```

but did not assign explicit component streams.

Therefore the channel's delivery sequence depended on the global automatic stream index and object-allocation order.

This was the core of G4-RNG-003 / G4-RNG-005.

## R3 repair

The driver now resets the automatic stream index at each independent repetition:

```text
RngSeedManager::ResetNextStreamIndex()
```

and assigns explicit component namespaces:

```text
channel stream = 10

mobility stream base   = 1000
mobility stream stride = 16 per node
```

`WsnChannel::AssignStreams()` consumes exactly one stream.

RandomWalk2d and RandomWaypoint receive deterministic per-node stream blocks.

## Regression

```text
leo-r3-channel-stream-identity = PASS / RC=0
```

The channel random sequence is now tied to an explicit stream identity, not incidental construction order.

---

# 5. Deterministic replay qualification

Two independent executions were run for each mobility mode using the same configuration and seed.

The resulting CSV files were compared byte-for-byte.

Results:

```text
none:
SHA256 = 6ad7e926efb9d00cb4a42138a5a349af78246616efa3411d28802950bf04f534
pair comparison = BYTE IDENTICAL

walk:
SHA256 = b12b07ecf60652c3c4ecfe273185e96e17adfbfb6e320bdfb241d76724fcc89a
pair comparison = BYTE IDENTICAL

waypoint:
SHA256 = 587df7bd23697798b44c06110671cfce3684929c2ffd5cc287ec7770fedd82f7
pair comparison = BYTE IDENTICAL

REPLAY_ALL = PASS
```

This establishes reproducible component/run identity for the tested execution path.

It does **not** claim that different routing metrics consume the same number of random draws after their protocol trajectories diverge.

The statistical pairing design itself is frozen later in R4/R6.

---

# 6. Fail-fast simulation-input validation

R3 rejects dangerous values before node creation/simulation/output mutation.

Runtime rejection matrix:

```text
nNodes=0                    -> rejected / PASS
runs=0                      -> rejected / PASS
spacingM=0                  -> rejected / PASS
bitrateBps=0                -> rejected / PASS
interferenceDb=-1           -> rejected / PASS
emaAlpha=2                  -> rejected / PASS
diagnostics period=0        -> rejected / PASS
output/diagnostic collision -> rejected / PASS
ackTimeoutS=0               -> rejected / PASS
discoveryTimeoutS=0         -> rejected / PASS
unwritable output path      -> rejected / PASS

FAILFAST_PASS = 11
FAILFAST_FAIL = 0
```

The invalid cases abort rather than producing plausible-looking scientific output.

Additional code-level guards cover:
- `nNodes >= 2`;
- finite positive geometry/environment parameters;
- finite background noise;
- non-negative interference penalty;
- finite positive bitrate;
- non-negative radio overhead;
- bounded retry field;
- finite mobility speed/pause ranges;
- non-negative mobility warmup;
- EMA in `[0,1]`;
- non-negative discovery window;
- non-empty output paths;
- successful CSV open.

---

# 7. Missing-MobilityModel fail-closed behavior

The former behavior could silently substitute an artificial distance when a node lacked a MobilityModel.

R3 removes that fallback.

Dedicated runtime probe:

```text
two Node objects
no MobilityModel installed
WsnChannel::SendUnicast(...)
```

Result:

```text
RC = 134
message =
WsnChannel requires a MobilityModel on both endpoint nodes:
sender=0 hasMobility=0 receiver=1 hasMobility=0

MISSING_MOBILITY_FAIL_CLOSED = PASS
```

A broken simulation configuration can no longer fabricate a strong 1 m link.

Closure:
- G1-CFG-004.

---

# 8. Honest WsnChannel object contract

## Prior defect

`WsnChannel` inherited from `ns3::Channel`.

But the custom `WsnNetDevice` is not an `ns3::NetDevice`, so the inherited generic `Channel::GetDevice()` contract could not be implemented truthfully.

The implementation returned `nullptr`, creating a semantic integration mismatch.

## R3 repair

`WsnChannel` now inherits from:

```text
ns3::Object
```

and provides the typed accessor:

```text
GetWsnDevice(std::size_t)
```

Regression:

```text
leo-r3-channel-contract = PASS / RC=0
```

Closure:
- G1-API-001.

This is an intentional custom application-level medium abstraction rather than a fake generic ns-3 Channel implementation.

---

# 9. Device-channel ownership cycle

## Runtime finding during R3

Sanitizer instrumentation exposed a real strong-reference cycle:

```text
WsnChannel -> Ptr<WsnNetDevice>
WsnNetDevice -> Ptr<WsnChannel>
```

The R3 development run documented:

```text
4320 bytes leaked
64 allocations
```

## Repair

Commit:

```text
f5959c9  fix: break device-channel reference cycle
```

The device back-reference is now explicitly non-owning:

```text
WsnChannel* m_channel
```

while `WsnChannel` owns attached devices for the configured run lifecycle.

This complements the R1 `DoDispose()` cleanup.

Closure:
- G3-LIFE-001;
- G13-LEAK-004.

---

# 10. Sanitizer requalification after repair

A monolithic executable was built directly from the repaired leo-wsn model sources plus `leo-topologies.cc` with:

```text
-fsanitize=address,undefined
-fno-omit-frame-pointer
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
```

Qualification modes:

```text
static/none      -> PASS
bounded walk     -> PASS
random waypoint  -> PASS
```

Current rerun result:

```text
SANITIZER_BUILD = PASS
SAN_none         = PASS
SAN_walk         = PASS
SAN_waypoint     = PASS
SANITIZERS_ALL   = PASS
```

No current:
- AddressSanitizer error;
- LeakSanitizer report;
- UBSan runtime error

was observed in those qualification runs.

This is bounded runtime evidence, not proof that every possible input is sanitizer-clean.

---

# 11. Combined R1 + R2 + R3 regression

Authoritative regression rerun on the R3 head:

```text
leo-r1-link-usable                         RC=0
leo-r1-discovery-timeout                   RC=0
leo-r1-stale-discovery-reply               RC=0
leo-r1-stale-ping-reply                    RC=0
leo-r1-matching-ping-reply                 RC=0
leo-r1-arq-route-invalidation              RC=0
leo-r1-frame-counter-reset                 RC=0
leo-r1-lifecycle-cleanup                   RC=0
leo-r1-delivery-context                    RC=0

leo-r2-physical-tx-quantization            RC=0
leo-r2-metric-realized-tx-power            RC=0
leo-r2-ack-tx-quantization                 RC=0
leo-r2-failed-rx-energy                    RC=0
leo-r2-ack-timeout-listening-energy        RC=0
leo-r2-effective-snr-lqi                   RC=0
leo-r2-eq17-policy                         RC=0

leo-r3-channel-stream-identity             RC=0
leo-r3-channel-contract                    RC=0

SUITES   = 18
FINAL_RC = 0
```

No R1 or R2 regression was reintroduced by R3.

---

# 12. What R3 closes

R3 closes or materially resolves:

```text
G4-RNG-003   RNG stream allocation instability
G4-RNG-005   construction-order RNG dependence
G1-CFG-004   silent 1 m missing-mobility fallback
G1-API-001   misleading generic ns3::Channel contract
G3-LIFE-001  strong channel/device ownership cycle
G13-LEAK-004 repeated-run ownership leak consequence
G12-CLI-001  major unsigned/unsafe input paths through fail-fast driver validation
G12-TIME-002 invalid mobility/warmup timing paths
G12-PHY-003  invalid bitrate/overhead paths
G12-PHYS-004 invalid geometry/environment paths
G12-INTF-007 negative interference semantic inversion
G12-RUN-008  zero-run success path
```

Some G12 findings were transformed from broad adversarial risks into explicit rejected inputs rather than silently accepted semantics.

---

# 13. Findings deliberately NOT closed by R3

## R4 — Experiment Design Freeze
Still open:
- exact primary source-study reconstruction subset;
- exact extension/sensitivity subsets;
- one machine-readable experiment manifest;
- exact relay-cap runtime parameter and recording;
- common metric/run pairing definitions;
- exact energy-accounting mode;
- Eq.17 bounded/literal production choice;
- `macMaxRetries`;
- `ackTimeoutS`;
- `discoveryTimeoutS`;
- `radioOverheadS`;
- `interferenceDb`;
- topology/spacing assumptions;
- source-fidelity pending items.

## R5 — Raw regeneration
No new production raw data have been generated.

## R6 — Statistics
Current v1.0.0 significance/discrimination outputs remain historical only.

## Remaining architectural/scalability work
R3 does not redesign:
- O(N) unicast device lookup;
- O(N^3)-class dense discovery campaign behavior;
- long-history route/flood state pruning;
- full native PHY/MAC replacement.

Those remain limitations/optimization items unless later gates decide they block the target experiment scale.

---

# 14. Scientific-data consequence

R3 changes stochastic identity, invalid-input behavior, object lifecycle, and channel configuration semantics.

Therefore:

```text
v1.0.0 raw data = HISTORICAL ONLY
patching/relabeling old data as v1.1.0 = FORBIDDEN
production raw regeneration = still CLOSED
```

R4 must freeze the experiment manifest first.

---

# 15. Remote repository status

Repair work remains on the authorized local clone:

```text
/home/pharmaco/projects/LEO_v1.1.0_Repair
branch = repair/v1.1.0
```

Earlier GitHub write attempt returned:

```text
403 Resource not accessible by integration
```

Therefore no claim is made that these repair commits are pushed to GitHub.

---

# 16. External-review status

ASTRA was not run because no callable ASTRA binding/provider is available in the active environment.

No substitute reviewer was represented as ASTRA.

---

# 17. R3 gate decision

```text
R3 = PASS
R3 = CLOSED
R4 = OPEN
SCIENTIFIC_RERUN = CLOSED
```

R3 exit evidence:
- explicit RNG identities implemented;
- deterministic same-seed replay passes in static/walk/waypoint;
- 11/11 fail-fast adversarial cases reject invalid input;
- missing mobility fails closed;
- custom channel contract is truthful;
- channel/device strong cycle is removed;
- current ASan/LeakSanitizer/UBSan runs pass in three mobility modes;
- combined R1+R2+R3 suite is 18/18 PASS with FINAL_RC=0;
- working tree was clean before closeout.

---

# 18. Next allowed action

Open:

```text
R4 — Experiment Design Freeze
```

R4 may define and validate manifests/configurations.

R4 may run only tiny preflight/qualification cases.

Production-scale scientific regeneration remains forbidden until R4 is formally closed.
