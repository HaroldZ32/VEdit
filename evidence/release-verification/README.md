# Post-release verification — the published bytes actually run

**Verification target: the public GitHub Release `v0.1.0` assets**
(`https://github.com/HaroldZ32/VEdit/releases/tag/v0.1.0`), downloaded with
`gh release download v0.1.0`. **Nothing from this repository's `packaging/*/out` was mounted,
copied or executed here** — the container's only artifact mount is the download directory.

## What was run

1. `gh release download v0.1.0 -D target/release-verify-download` (all four assets).
2. `sha256sum -c SHA256SUMS` on the downloaded files — the manifest itself came from the Release.
3. A clean, **offline** CentOS 7 container (`--network none`), image with **no gtk3 /
   GtkSourceView rpm** (negative assertion re-run inside the container), Xvfb display.
4. For **each of the three products**: `--selftest all` (6 suites, **51 checks**), exit code
   asserted 0, plus the checksum of the `vd-app` *inside the downloaded payload*.

## Result

| Product | Downloaded sha256 == SHA256SUMS | `--selftest all` | checks | in-payload `vd-app` | Conclusion |
|---|---|---|---|---|---|
| `VEdit-0.1.0-x86_64.AppImage` (47,745,528 B) | `4f800ab9…ce44` — **OK** | exit **0** | **51/51** | `41d02dca…1611` | runs, matches the frozen binary |
| `vedit-0.1.0-linux-x86_64.tar.gz` (40,493,353 B) | `ddf38973…e6e81` — **OK** | exit **0** | **51/51** | `41d02dca…1611` | runs, matches the frozen binary |
| `VEdit-0.1.0-x86_64.run` (40,496,151 B) | `0ef44b35…788ea` — **OK** | exit **0** | **51/51** | `41d02dca…1611` | runs, matches the frozen binary |

The `.run` also cleaned up its extracted tree after exit (no `/tmp/vedit.*` left). The image
reported `glibc 2.17-317.el7` and no GTK rpm, so the GTK that ran came from the payload.

**Conclusion: the bytes published on the GitHub Release have been verified to run in a clean,
offline CentOS 7 environment; the core binary inside all three downloads is identical to the
frozen canonical `vd-app` (`41d02dca…`).**

## Evidence

| File | Content |
|---|---|
| `00-assets.txt` | release URL, tag commit, sha256 + size of every downloaded file, in-payload `vd-app` hashes |
| `01-download.txt` | the exact `gh release download` / `ls` / `sha256sum -c` output |
| `01-hashes.txt` | `vd-app` sha256 extracted from each downloaded product |
| `10-appimage-selftest.log` | AppImage `--selftest all` (raw, 51 checks) |
| `11-targz-selftest.log` | tar.gz launcher `--selftest all` (raw, 51 checks) |
| `12-run-selftest.log` | `.run` `--selftest all` (raw, 51 checks) |
| `90-run.log` | the full in-container run with every PASS line |
| `99-results.txt` | machine-readable results (`RESULT <name>=PASS|FAIL`, `FAILURES=0`) |
| `verify.sh`, `run.sh` | the verification and its host entry point |

## Reproduce

```sh
bash evidence/release-verification/run.sh      # downloads from GitHub, then verifies offline
```

It needs `gh` authenticated for the download and Docker for the container; the container itself
runs with `--network none`.

## Note on what this does and does not cover

This proves **delivery integrity and runnability of the published artifacts**: the upload is
byte-identical to the reviewed artifacts, the three products start and pass their own selftest
in a clean offline environment, and the core binary matches the frozen canonical one. It does not
re-test the individual AC items beyond that selftest suite (their lane evidence and the e2e
report stand on their own), and it does not cover a real desktop's IME/window-manager behaviour.
