# SIGPIPE-under-pipefail audit (T58)

Scope: every shell script under `scripts/**`, `build/gates/**`, `build/slang/**`,
`packaging/**` (the directories the task names, plus the scripts the gates call).
Method: search for pipelines whose **consumer exits before its input ends** —
`grep -q`, `grep -m`, `grep -l`, `head` — because only an early exit can make a
producer die on a broken pipe. Under `set -o pipefail` (or `set -euo pipefail`,
which every gate script inherits from `build/gates/_common.sh` /
`scripts/lib.sh`) that producer status becomes the pipeline status, so a perfectly
fine check can report failure. `00-raw-audit.txt` is the same search on the
current tree (after the fixes); the as-found list is the table below, captured
with the same patterns before the edits.

66 pipeline sites were found and classified (30 `grep -q`/`-m`, 36 `head`;
counted after the audit patterns were applied to the pre-fix tree). **10 sites
were fixed** (9 changes below; one change covers two lines); the rest are left
with the reason they cannot trigger (bounded producer output well under the
64 KiB pipe buffer, an explicit `|| true`, or no `pipefail` in that script so a
producer's status can never become the pipeline status).

Two classes of damage, not one:

* **false RED** — a present symbol/font/dir reads as missing (the reported bug);
* **false GREEN** — a `find | grep -q` that is supposed to *fail* when something
  forbidden exists returns non-zero (SIGPIPE) on a match, so the "bad" branch is
  skipped. That is the worse failure and it sat in two packaging gates
  (`verify-not-shipped.sh`, `assemble-appdir.sh`) and in the embedded-definition
  detector (legal/compliance checks).

## Fixed (10 sites, 9 rows)

| # | file:line (current) | as found | failure mode | fix |
|---|---|---|---|---|
| 1 | `scripts/build-qt-in-container.sh:96` | `nm -C "$APP" \| grep -qE ' T vd_abi_version$'` | **false RED** on the real binary: `nm` is killed by SIGPIPE(141) after `grep -q` matches, `pipefail` reports the pipeline as failed → the recorded `FATAL: vedit-app does not export vd_abi_version` while `nm` printed the symbol and `## count 36` (see `evidence/project-model/17-typecheck-gate.log` lines 526–572). Producer size: full symbol table, unbounded by design. | `grep -E … > /dev/null` — grep reads to EOF, `nm` exits normally, status is the match result only. FATAL unchanged. |
| 2 | `build/gates/pack.sh:29` | `printf '%s\n' "$entries" \| grep -qE '…launcher…'` | **false RED**: an archive with thousands of entries can exceed the pipe buffer; the gate would report "no launcher entry". Runs under `_common.sh`'s pipefail. | `grep -E … > /dev/null`. |
| 3 | `packaging/tarball/stage.sh:313` | `strings "$PKG/bin/vd-app" \| grep '/vedit-build' \| sort -u \| head -5` | **false RED**: `strings` of the ~70 MB product binary is huge; `sort -u` can still be writing when `head` exits → script (pipefail) aborts while listing a WARN. | `sed -n '1,5p'` (reads to EOF, prints the same 5). |
| 4 | `scripts/build-ksh-selfbuild-in-container.sh:159` | `comm -13 "$DISTRO_SYMS" "$OUR_SYMS" \| head -20` | **false RED** on an informational symbol diff: `comm` output can exceed the buffer. | `sed -n '1,20p'`. |
| 5 | `packaging/runtime/build.sh:44` | `(ldconfig -p \| grep -m1 libfuse) \|\| echo "libfuse not visible"` | **false DIAGNOSTIC**: the whole library cache is printed; `grep -m1` exits early → SIGPIPE → the `\|\|` branch claims libfuse is missing while it is installed. Script uses `set -uo pipefail`. | `grep libfuse \| sed -n '1p'` (grep reads all; one-line output kept). |
| 6 | `packaging/appimage/clean-room-accept.sh:257` | `fc-list :lang=zh \| grep -q 'wqy-zenhei'` | **false RED**: the font list can be long; a present bundled font would be reported missing (script has `set -uo pipefail`). | `grep 'wqy-zenhei' > /dev/null`. |
| 7 | `packaging/runtime/assemble-appdir.sh:154` | `! find "$OUT" -type d -name 'syntax-bundled' \| grep -q . \|\| { echo ERROR; exit 1; }` | **false GREEN**: when the forbidden dir EXISTS, `grep -q` matches and exits, `find` gets SIGPIPE → pipeline non-zero → `!` turns it into success → packaging continues with a C5 violation. | `find … -print -quit` inside `[ -n "$(…) " ]` — no pipeline at all, same detection strength. |
| 8 | `packaging/runtime/verify-not-shipped.sh:57,64` | `if find "$APPDIR" -type f -name "$n" \| grep -q .` (and same for the dir) | **false GREEN** in a compliance gate: a shipped `systemverilog.xml`/`verilog.xml`/`syntax-bundled/` matches, the pipeline fails from SIGPIPE, the `if` takes the else branch and reports clean. | `[ -n "$(find … -print -quit)" ]`. |
| 9 | `packaging/runtime/verify-not-shipped.sh:115` | `strings -a -el "$LIB" \| grep -qx 'systemverilog.xml'` | **false GREEN**: `strings` on the KSH library is a large stream; an embedded definition resource could be reported absent. | `grep -x … > /dev/null`. |

## Left as-is (bounded / no pipefail / guarded) — 57 sites

### `grep -q`/`-m` pipelines (22 left)

| file:line | producer | why it cannot trigger |
|---|---|---|
| `scripts/build.sh:106` | `readelf -d \| sed -n '/NEEDED/p'` | NEEDED entries of one binary: a few lines, far below 64 KiB |
| `scripts/lib-qt.sh:118` | `docker info --format '{{json .SecurityOptions}}'` | one short JSON array; and `lib-qt.sh` sets no `pipefail`, so a producer status cannot become the pipeline status |
| `build/gates/dist.sh:25` | `file "$appimage"` | one line |
| `build/gates/gates.sh:34` | `readelf -d \| sed` | NEEDED list: a few lines |
| `build/slang/scripts/verify-offline.sh:73` | `ldd /out/slang` | ~20 dependency lines |
| `build/slang/build.sh:90` | `objdump -p \| sed -n '/NEEDED/p'` | NEEDED list: a few lines |
| `packaging/tarball/clean-room.sh:59,196` | `xwininfo -root -tree` | one screen's window tree: dozens of lines; script is `#!/bin/sh` with `set -u` only (no pipefail) |
| `packaging/tarball/clean-room.sh:319,324` | `awk … "$EVID/35-processes.txt"` | process list file: tens of lines; no pipefail |
| `packaging/tarball/stage.sh:74,88` | `readelf -S "$APP"` | section headers: ~2–3 KiB |
| `packaging/appimage/clean-room-accept.sh:187,196` | `xwininfo -root -tree` | bounded window tree |
| `packaging/appimage/clean-room-accept.sh:282` | `grep -Eiv … processes.txt` | process list file: tens of lines |
| `packaging/appimage/check-rpaths.sh:27`, `glibc-gate.sh:17` | `head -c 4 "$file"` | producer emits exactly 4 bytes, then EOF: the consumer never closes early |
| `packaging/runtime/verify-f2-isolation.sh:81` | `printf '%s' "$names"` | a short list of library names |
| `packaging/runtime/selfcheck.sh:55–58` (4) | `printf '%s' "$OUT"` | one smoke run's output: a few KiB |

### `head` pipelines (34 left)

| file:line | producer | why it cannot trigger |
|---|---|---|
| `scripts/fetch-verible.sh:63` | `compgen -G` | few matches **and** an explicit `\|\| true` |
| `scripts/run-ksh-form-experiment-in-container.sh:88,96,97,123,175,207,208` (7) | `grep … <logfile>` | first command reads a file; the matching lines are a handful (the experiment logs are short) |
| `scripts/build-qt-in-container.sh:29,34,35` (3) | `ldd --version`, `cmake --version`, `g++ --version` | a few lines of each |
| `scripts/check-tree-sitter-cli.sh:67` | `"$CLI" --version` | a few lines |
| `scripts/build-ksh-selfbuild.sh:83` | `find … -name 'libKF6SyntaxHighlighting.so.6.*'` | one or two matches |
| `scripts/build-ksh-selfbuild-in-container.sh:80` | same | one or two matches |
| `scripts/build-ksh-selfbuild-in-container.sh:204` | `grep -o '<language…>' <xml>` | matches bounded by one small XML file |
| `scripts/build-in-container.sh:182` | `compgen -G` | few matches **and** `\|\| true` |
| `packaging/tarball/clean-room.sh:46,47,66,141,296` (5) | `ls`/`grep`/`sed` on files | few entries; no pipefail |
| `packaging/tarball/stage.sh:114,118` | `find …` | few matches |
| `packaging/appimage/clean-room-accept.sh:66` | `ls *.AppImage` | one artifact |
| `packaging/appimage/clean-room-accept.sh:205` | `xwininfo \| grep` | bounded window tree |
| `packaging/appimage/clean-room-accept.sh:220,222` | `sh -x AppRun --version` (xtrace) | a short launcher run; the script has no `set -e` and neither `head` pipeline's status is checked |
| `packaging/appimage/clean-room-accept.sh:251` | `fc-list :lang=zh` inside a redirect block | output is truncated by design for a human listing (the count on the next line uses `wc -l`, which reads all); no `set -e`, status unchecked |
| `packaging/appimage/build-appimage.sh:87` | `find …` | few matches |
| `packaging/appimage/build-appimage.sh:147` | `ldd \| head -1` | one line **and** `\|\| true` |
| `packaging/appimage/build-appimage.sh:329` | `printf '%s\n' "$host_hits"` | short list |
| `packaging/appimage/build-appimage.sh:336` | `tr \| grep -a -m 2` | **and** `\|\| true` |
| `packaging/runtime/verify-f2-isolation.sh:75,90` | `printf '%s\n' "$out"` | short KSH-ENV output |

## Patterns that are safe by construction (not listed above)

`grep -c`, `tail`, `awk … END`, and `sed -n '1,Np'` read their input to EOF, so
they never close the pipe early; `… | tee file` reads all. These are used widely
(e.g. `nm \| grep -c`, `readelf -V \| sort \| tail -1`, the `sed -n '1,Np'`
replacements added here) and need no change.

## Why fix the "left" ones is not worth it

Each left site has a producer whose **complete output is smaller than the 64 KiB
pipe buffer** (or an explicit `|| true`, or no pipefail). SIGPIPE can only be
raised by a write *after* the reader closed; a producer that finishes writing
before the reader exits never sees it. Those sites are documented rather than
churned, so a future change that makes one of those producers large (e.g. a much
larger `readelf`/`ldd` output) is the moment to fix it — this table is the
inventory for that review.

## Falsification of the fix

`01-negative-check.log` (produced by `run-negative-check.sh`) proves both
directions:

* the mechanism is real and deterministic (an early-matching, still-writing
  producer under `pipefail` + `grep -q` is a false failure; the fixed form
  passes the same stream);
* the fixed predicate **still fails** on a compiled shim that exports five
  `vd_*` symbols but not `vd_abi_version`, and on `/bin/true`; the second FATAL
  (<20 `vd_*` symbols) also still fires. So the fix removes a false negative in
  the *shell*, not a check.
