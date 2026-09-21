# v1.1.0 — Human Release Gate Closeout

## Program
LEO v1.1.0 — Repair & Requalification

## Status
`LOCAL_RELEASE_GATE_PASS / REMOTE_PUBLISH_BLOCKED_BY_PERMISSION`

## Human decision
The final author list and ordering in `CITATION.cff` were explicitly confirmed by the user.

Confirmed order:
1. Samir Othmani — University of El Oued — ORCID 0000-0002-1893-0076
2. Okba KAZAR — University of Kalba, Shajah, UAE
3. Nedjoua Houda KHOLLADI — University of El Oued
4. Khaoula BELILA — University of El Oued

Author-confirmation commit:

```text
6f4185297e8f2c609a0a19630566d75588ca076e
```

CITATION.cff SHA256:

```text
cc97532909669e321e31c1b7f946d52b8a0ca94a8e5d7890333847b45650bb45
```

The temporary author-list warning was removed after confirmation.

## Technical release state
R0–R7: PASS / CLOSED

R8:
```text
PASS_WITH_RELEASE_HUMAN_GATE / CLOSED
```

R8 technical/scientific eligibility remains valid. The author-list human gate is now resolved.

R8 final report SHA256:

```text
d5ffbf6603b4f50876bca47694abab2fbc17465fe015b459901d18da0f576a2a
```

R8 final QC SHA256:

```text
f5c9f752b2dba227470f7c16b240cc9387aa4ec4574db86165388fda6acd414a
```

## Dataset release asset
Local asset:

```text
/home/pharmaco/projects/LEO_v1.1.0_Release_Assets/leo-v1.1.0-results.tar.gz
```

Size:

```text
13829224 bytes
```

SHA256:

```text
e32b25adcca49abcbe732ba75d80390d07542128395fca5332af3235cded3783
```

gzip integrity was rechecked before release-gate closeout.

Release notes SHA256:

```text
762e2a2f4694f7d0ae8eedc140e6ae32e462cc3cb1005a18872df9a3f3e23ae7
```

## Remote publication permission
The active GitHub credential was checked against:

```text
othmani-samir-gh/leo-metric-ns3-reproduction
```

GitHub reported:

```text
pull = true
push = false
```

Therefore the assistant cannot truthfully publish the branch, tag, release, or dataset asset to the public repository using the currently authorized credential.

This is an authorization boundary, not a scientific or technical failure.

## Local release action
A local annotated `v1.1.0` tag may be created after this closeout commit.

Public release remains pending one remote-authorized action:
- use a GitHub account/token with push/release permission for the repository;
- push the final commit/tag;
- create the GitHub release;
- upload `leo-v1.1.0-results.tar.gz`;
- verify the uploaded asset SHA256 equals `e32b25adcca49abcbe732ba75d80390d07542128395fca5332af3235cded3783`.

## Final decision
```text
SCIENTIFIC_REQUALIFICATION = COMPLETE
AUTHOR_GATE = RESOLVED
LOCAL_RELEASE_CANDIDATE = APPROVED
PUBLIC_GITHUB_RELEASE = BLOCKED_BY_REMOTE_PERMISSION
NO_R9_REQUIRED
```
