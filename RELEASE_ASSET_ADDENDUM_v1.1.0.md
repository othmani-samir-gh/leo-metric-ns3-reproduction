# v1.1.0 release asset — packaging addendum

This addendum is committed after the `v1.1.0` tag and does not modify any
tagged file. It records how the dataset asset attached to the v1.1.0 GitHub
Release was produced, because it differs at the archive level from the asset
described in the tagged `RELEASE_ASSET_MANIFEST.json`.

## What differs, and what does not

| | Tagged manifest | Attached asset |
|---|---|---|
| File name | `leo-v1.1.0-results.tar.gz` | `leo-v1.1.0-results.tar.gz` |
| Archive SHA-256 | `e32b25adcca49abcbe732ba75d80390d07542128395fca5332af3235cded3783` | `fe1a1f6a1e9ab40758ee07de71eac841e95328ed542d13c55e53169f79a03ce5` |
| `PROVENANCE.json` SHA-256 | `ed84f061a646...` | `ed84f061a646...` (identical) |
| `CHECKSUMS.sha256` SHA-256 | `6c584cf361be...` | `6c584cf361be...` (identical) |
| `R5_INTEGRITY.json` SHA-256 | `b018228161ab...` | `b018228161ab...` (identical) |
| Uncompressed size | 532,911,614 bytes | 532,911,614 bytes (identical) |

**The scientific content is identical.** Only the archive wrapper differs.

## Why the archive hash differs

A `.tar.gz` hash depends not only on file contents but on archive metadata:
file modification times, ownership, member ordering and the gzip header. The
original asset was packaged in the audit environment, and the recipe used was
not recorded. The released directory was later transferred through an
intermediate archive format that did not preserve the original metadata, so the
original bytes could not be regenerated. The dataset was therefore repackaged.

The content hashes in the table above are the binding scientific identities:
they are the values recorded in the tagged manifest, and they are unaffected by
repackaging.

## Deterministic packaging recipe

Unlike the original, the attached asset is reproducible. From the repository
root, with `results/v1.1.0/` present:

```bash
M=$(git log -1 --format=%ct 82cd0fa)
tar --sort=name --mtime=@$M --owner=0 --group=0 --numeric-owner \
    --mode='u+rwX,go+rX,go-w' -cf - results/v1.1.0 | gzip -n -6 \
    > leo-v1.1.0-results.tar.gz
sha256sum leo-v1.1.0-results.tar.gz
# expected: fe1a1f6a1e9ab40758ee07de71eac841e95328ed542d13c55e53169f79a03ce5
```

The timestamp is pinned to commit `82cd0fa` (the commit that prepared the
dataset asset). Byte-identical output assumes GNU tar with `--sort` support
and GNU gzip; other implementations may differ at the archive level while
still producing identical extracted content.

## Verification

This is the same procedure as the tagged `RELEASE_ASSETS.md`, and it does not
depend on the archive hash:

```bash
sha256sum -c leo-v1.1.0-results.tar.gz.sha256
tar -xzf leo-v1.1.0-results.tar.gz          # at the repository root
sha256sum -c results/v1.1.0/CHECKSUMS.sha256
```

Expected: all 2,478 entries report `OK`. Three of them
(`build/r5/leo-topologies`, `experiments/v1.1.0/frozen_plan.jsonl`,
`experiments/v1.1.0/manifest.json`) are repository files rather than archive
members, which is why extraction must happen at the repository root.

This procedure was run on the attached asset before release: 2,475 archive
entries and 3 repository entries, all `OK`.
