# W2 spike — slang-server on CentOS 7 + hierarchical elaboration (AC-1.5 / AC-3.0)

Cell `cell-20261102T050548232Z`, board task **T4**;
contract `.mpd/cellwork/cell-20261102T050548232Z/contracts/spike-slang.md`.

## VERDICT: **PASS** (both halves), on the fixed fixture

* **AC-1.5** — `slang-server` **runs on CentOS 7 / glibc 2.17 and completes a real
  `initialize` handshake** (0.189 s, `serverInfo=0.3.1+2ad996f`, 15 capability
  groups). The **upstream prebuilt cannot** (needs `GLIBC_2.27/2.25/2.18`), so the
  artifact is a **source build** with the repository's existing CentOS-7 recipe;
  the contract explicitly allows "预编译 slang-server-linux-x64 **或源码构建**".
* **AC-3.0** — on the required fixture (top instantiates mid twice; the signal is
  driven in mid and loaded in top) the elaborated **instance hierarchy** and the
  **signal driver/load** are both obtained, far inside the kill bound:
  CLI `slang --ast-json`: **4 ms / 19.0 MB**; server (`slang.setBuildFile` +
  `getDrivers/GetLoads`): **2 ms / 25.6 MB peak RSS**. Bound: 30 s / 1 GiB.

No C-plan fallback is needed. The C-plan is therefore **not** activated.

## Reproduce (one command, from the repository root)

```bash
bash probes/slang-spike/run-spike.sh
```

That is the whole reproduction: build (or reuse) → `--version` in a clean centos:7 →
`initialize` with the server inside the container → elaboration (CLI) → hierarchy
through slang-server's own LSP commands. Nothing is hand-run, and every mount and
flag the handshake needs is in the script:

* the fixture is bind-mounted at `/fixture` and the client announces
  `rootUri=file:///fixture` — without both, the server answers
  `cannot set current path: No such file or directory`;
* the generated `.f` files go to `target/tmp/spike-work/buildfiles/`, never into
  the pinned `probes/slang-spike/fixture/`.

The script prints `step2_exit=… step3_exit=… step4_exit=… step4b_exit=…` and a final
verdict, and exits non-zero if any step failed. The recorded run:

```text
step2_exit=0  step3_exit=0  step4_exit=0  step4b_exit=0
SPIKE REPRODUCTION PASSED — evidence under …/evidence/w2-spike
```

Artifacts: `target/tmp/spike-work/out/slang-server` (sha256 `f691600f…`, 11.7 MB
stripped — the 5.2 MB upstream figure is the COMPRESSED download, a different
measurement). The steps are documented in `docs/BUILD.md` §13 (new section; existing
text untouched).

## Evidence files

| file | what it proves |
| --- | --- |
| `01-environment.txt` | host/docker/glibc of the target image, repo `slang` version, the prebuilt's GLIBC symbol set |
| `02-prebuilt-centos7-run.txt` | **prebuilt fails** on glibc 2.17 (`GLIBC_2.27/2.25/2.18 not found`, exit 1) |
| `03a-build-toolchain.txt` | the build container: CentOS 7.9.2009, glibc 2.17, gcc 11.2.1 (devtoolset-11), 16 cpus |
| `03b-build-source.txt` | pinned commit `2ad996f…` + submodule commits (slang fork, reflect-cpp, ctre) |
| `03c-build-configure.log` | configure with static libstdc++/libgcc and the GCC-11.2 path-hash `-include` |
| `03d-build-compile.log` | full compile; the only warning is an unused parameter inside reflect-cpp |
| `03e-build-artifact.txt` | artifact size, **highest GLIBC symbol 2.17**, only libm/libpthread/libc |
| `03f-build-timings.txt` | **440 s total** on 16 cores |
| `03-built-centos7-version.txt` | the built binary in a **clean** centos:7: `slang-server version 0.3.1+2ad996f`, exit 0 |
| `04-built-initialize-handshake.txt` | **real `initialize`** with the server **inside** centos:7 (fixture mounted at `/fixture`, `rootUri=file:///fixture`): 0.191 s, serverInfo + 15 capability groups, plus its `slang/setConfig` notification |
| `05-elaboration.json` | CLI elaboration: hierarchy `top → top.u0/top.u1` (module `mid`, instantiation file:line:column), `top.link` drivers/loads, full driver chain, **4 ms / 19.0 MB** |
| `05-elaboration-console.txt` | the same run as printed by the probe |
| `06-hierarchy-lsp-shape.txt` | the **product path**: raw `slang.getScopes` / `getScopesByModule` / `getInstancesOfModule` / `searchHierarchy` / `getDriversWithLocation` / `getLoadsWithLocation` JSON, plus server measurements (**2 ms / 25.6 MB**) |
| `07-buildfile-rule.txt` / `.json` | the measured build-file/cwd rule: four layouts (`.f` inside/outside the root, relative/absolute entries, cwd=root vs cwd=parent). **Only the cwd changes the answer** |

## The shape T7 needs (verbatim excerpts)

Hierarchy (LSP `slang.getInstancesOfModule("mid")`):

```json
[{"instPath":"top.u0","instLoc":{"uri":"file:///…/fixture/top.sv",
   "range":{"start":{"line":10,"character":6},"end":{"line":10,"character":43}}}},
 {"instPath":"top.u1","instLoc":{"uri":"file:///…/fixture/top.sv",
   "range":{"start":{"line":11,"character":6},"end":{"line":11,"character":…}}}}]
```

Driver / load of `top.link` (LSP):

```json
slang.getDriversWithLocation("top.link")
[{"path":"top.u0.q","location":{"uri":"file:///…/fixture/mid.sv",
  "range":{"start":{"line":4,"character":17},"end":{"line":4,"character":18}}}}]
slang.getLoadsWithLocation("top.link")
[{"path":"top.observed","location":{"uri":"file:///…/fixture/top.sv","range":{…line 14…}}},
 {"path":"top.u1.d","location":{"uri":"file:///…/fixture/mid.sv","range":{…line 3…}}}]
```

The same information through `slang --ast-json` (`05-elaboration.json`), i.e. the
fallback that needs no server process:

```json
"hierarchy": [{"path":"top","module":"top","instantiation":{"file":"…/top.sv","line":3,"column":8}},
              {"path":"top.u0","module":"mid","instantiation":{"file":"…/top.sv","line":11,"column":7}},
              {"path":"top.u1","module":"mid","instantiation":{"file":"…/top.sv","line":12,"column":7}}],
"driver_chain_of_interest": {"top.link": [
  {"from":"top.link","to":"top.u0.q","kind":"instance-output-port","at":{"file":"…/top.sv","line":11,"column":38}},
  {"from":"top.u0.q","to":"top.u0.state","kind":"continuous-assign","at":{"file":"…/mid.sv","line":13,"column":10}},
  {"from":"top.u0.state","drivers":[{"via":"top.u0.state (AlwaysFF)","kind":"procedural","at":{"file":"…/mid.sv","line":10,"column":5}}]}]}
```

## How time and memory were measured

* CLI route: `time.monotonic()` around the child, and
  `resource.getrusage(RUSAGE_CHILDREN).ru_maxrss` (KiB on Linux) for the peak RSS —
  recorded by `probes/slang-spike/elab_probe.py` itself.
* Server route: wall time around the `slang.setBuildFile` executeCommand; peak RSS
  from `/proc/<pid>/status` `VmHWM` (the kernel's high-water mark) at the end of the
  probe (`probes/slang-spike/lsp_hierarchy_probe.py`).

## What this spike does NOT prove

1. **Scale.** The fixture is 2 instances / 2 files. AC-2.14 / AC-3.5 (1k files /
   10k instances) are not covered here; the kill bound in this contract applies to
   the fixed fixture only.
2. **Packaging.** The artifact is not added to the shipped package; the spike only
   answers "can it exist and run on the target".
3. **slang-server is a third-party dependency**, pinned to v0.3.1 (`2ad996f…`); an
   upstream change is a new spike, not a drop-in.
4. **UVM factory / dynamic generation** are out of scope (AC-3.5 says so).
5. The `initialize` handshake was run with a minimal client (`capabilities: {}`);
   editor-specific negotiation (inlay hints, code lens, workspace symbol) is not
   exercised here.

## Integration notes that came out of the measurements (important for T7)

* `slang-server`'s cwd must be the project root and the `initialize` `rootUri` must
  exist in the process's own filesystem (`cannot set current path` otherwise).
* **The server's cwd must be the project root, and `rootUri` must name a path that
  exists in that process.** The `.f` FILE may live anywhere (measured: outside the
  project root, absolute path — correct URIs); its ENTRIES are best written
  project-root-relative. What decides whether `location.uri` is correct is the cwd:
  with cwd = the PARENT of the project root the same run returns
  `…/slang-spike/mid.sv` instead of `…/fixture/mid.sv` — the module tree still looks
  right and the jump lands on the wrong file. Four measured combinations:
  `07-buildfile-rule.txt` (`probes/slang-spike/buildfile_rule_probe.py`).
* slang's AST JSON **deduplicates instance bodies**: the second instance of the same
  module serialises `"body": "<addr> <name>"` instead of a body copy. A consumer
  must resolve those references by address (the probe does).
