# Fixture evidence (C13 / AC-S1.5)

The repository-owned Verilog fixture and its reproducible acceptance script.
Nothing here was produced from a user's real project.

## What is being verified

- `tests/fixtures/verilog-project/` — the fixture: `project.f` with `+incdir+`,
  `+define+` (value and bare form), nested `-f` (two levels, resolved relative to
  the containing filelist), `//` and `#` comments, a duplicate entry, and two
  simulator-only tokens (`+libext+.v+.sv`, `-y`) that must be skipped with a
  warning; `.sv` and one plain `.v` source with a real hierarchy
  (`tb_top -> top -> {alu, counter, mem}`), a package, and four headers reached
  through the include directories.
- `tests/fixtures/verilog-project/expected/` — the resolved lists the script
  compares against: `files.txt`, `incdirs.txt`, `defines.txt`,
  `skipped-tokens.txt`, `duplicates.txt`.
- `scripts/verify-fixture.sh` — expands `project.f` with the semantics the
  product implements (`crates/vd-config/src/filelist.rs`), compares every list
  with `expected/`, and checks the fixture's structural claims (every resolved
  source exists, no `.v/.sv` is unreachable, every `` `include `` resolves
  through an `+incdir+`, every `+define+` name is used, hierarchy/package
  assertions).

## Files in this evidence directory

| file | content |
| --- | --- |
| `manifest.txt` | sha256 + size for every fixture source/filelist/header, plus the resolved entries, include dirs, defines, skipped tokens and duplicates |
| `expected.txt` | the checked-in expectations, verbatim |
| `verify-output.txt` | the full report of the recorded run (exit 0) |
| `idempotence.txt` | two consecutive runs, identical sha256 of their output |
| `failure-modes.txt` | four negative cases on throw-away copies, each exiting 1 |

Each file starts with the C9 header (capture time, `git rev-parse HEAD`,
`git status --porcelain`).

## Reproduction

```sh
bash scripts/verify-fixture.sh
bash scripts/verify-fixture.sh --evidence evidence/fixture
```

Observed on the recorded HEAD: exit 0, `21 passed, 0 failed, 1 skipped`.
The one SKIP is `verible-verilog-syntax`, which is not installed on this host:
the Verilog syntax parse of the fixture is therefore **not** verified here, and
is left to the toolchain-bearing runs (the pinned image or the user's machine).
That is a declared gap, not a passed check.

Negative modes (see `failure-modes.txt`), each on a copy, never on the tree:

| case | observed exit |
| --- | --- |
| an expectation claims a source the filelist does not list | 1 |
| a resolved source file is deleted | 1 |
| a nested `-f` target is missing | 1 |
| an `+incdir+` directory is deleted | 1 |

Idempotence: the script writes only under `--evidence` (overwriting, never
appending) and its plain-mode output is byte-identical across runs — sha256
`2f625093730042f12d1863505c25d9252418b79b5fde95ed9a02bb450aa3493e` twice.

No existing test or gate was modified. The repository gate `docs`
(`bash build/gates/docs.sh`) was re-run unchanged and passed (exit 0); the
compile gates need the pinned build image and were not run for this
documentation/fixture-only change.
