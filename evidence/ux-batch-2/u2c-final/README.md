# U2c — wrap the remaining user-visible Chinese literals (diagnostics + FIM)

Task: T36 (`U2c: wrap remaining user-visible Chinese literals (diagnostics + FIM)`),
follow-up to the U2b review's "carried, needs a follow-up decision" item.
Writer: Junior Engineer (`junior-engineer-2`) — the sole writer of
`translations/vedit_zh_CN.ts`.
Write scope touched: the diagnostics/FIM sources named by the task, the two FIM
self-tests (see §4), the catalogue, and this directory.

## 0. Result

- **22 new catalogue rows** (English source + the previous Chinese text as the
  translation), in the existing context `VEdit`, each with real `<location>`s.
- A CJK scan of `tr()`/`translate()` **first arguments** over `src/` returns **0**.
- `lrelease`: **1208 translation(s) (1208 finished and 0 unfinished)**, exit 0.
- FIM lane self-test rebuilt and run in the pinned image: **51 checks, 0 failures**.
- The two diagnostics status strings render their substitutions after translation
  (`已取消 — 120/1000 文件`, `120/1000 文件已扫描 — 37 条 CLI 诊断`).
- **One deviation, disclosed in §4**: three self-test *expectation literals* had to
  move from Chinese to the new English source — they assert a string the product
  now generates, so leaving them would have made two checks red. Nothing was
  weakened; the same checks assert the same behaviour.

## 1. What was wrapped

Pattern used everywhere: `QStringLiteral("中文")` →
`QCoreApplication::translate("VEdit", "English source")`, arguments and their
order unchanged. `fim_inline_completion.cpp` gained `#include <QCoreApplication>`
(it had none).

### 1.1 `src/workspace/diagnostics/diagnostics_controller.cpp` (2 strings)

| line | old | new source | zh translation |
|---|---|---|---|
| 156 (`lintCancelledMessage`) | `QStringLiteral("已取消 — %1/%2 文件")` | `cancelled — %1/%2 file(s)` | 已取消 — %1/%2 文件 |
| 560 | `QStringLiteral("%1/%2 文件已扫描 — %3 条 CLI 诊断")` | `%1/%2 file(s) scanned — %3 CLI diagnostics` | %1/%2 文件已扫描 — %3 条 CLI 诊断 |

Both are shown to the user: `pass_.message` is the project-pass status sentence
(the strip's terminal snapshot, `diagnostics_controller.h:129`). The `.arg()`
chain is unchanged, so `%1`/`%2`/`%3` keep their order.

### 1.2 `src/editor/fim/**` and the wiring (20 strings)

All of these end up in `InlineCompletion::statusText()` / `FimBinding::statusText()`,
i.e. the status line — user-visible, so all were wrapped. Line numbers are the
post-edit ones (they equal the `<location>` lines in the catalogue).

| file:line | new English source | zh translation |
|---|---|---|
| `fim_backend.cpp:51`, `fim_inline_completion.cpp:80` | FIM request was not accepted by the Rust service | FIM 请求未被 Rust 服务接受 |
| `fim_backend.cpp:71`, `fim_inline_completion.cpp:49` | FIM timed out (%1 ms without a result) | FIM 超时（%1 ms 无结果） |
| `fim_backend.cpp:79` | FIM service unavailable (could not create the Rust handle) | FIM 服务不可用（无法创建 Rust 句柄） |
| `fim_backend.cpp:91` | FIM request was not accepted by the Rust service: %1 | FIM 请求未被 Rust 服务接受：%1 |
| `fim_backend.cpp:127` | FIM failed: %1 | FIM 失败：%1 |
| `fim_backend.cpp:130` | FIM poll failed (state %1) | FIM 轮询异常（state %1） |
| `fim_inline_completion.cpp:19,29,112,154` | FIM disabled (off by default) | FIM 未启用（默认关闭） |
| `fim_inline_completion.cpp:20,30,98` | FIM unavailable (no Rust service linked) | FIM 不可用（未链接 Rust 服务） |
| `fim_inline_completion.cpp:66` | FIM unavailable (no Rust service linked; no request was sent) | FIM 不可用（未链接 Rust 服务；未发出请求） |
| `fim_inline_completion.cpp:84` | FIM request in flight… | FIM 请求中… |
| `fim_inline_completion.cpp:109,151` (2 sources) | FIM suggestion ready / FIM returned an empty suggestion | FIM 建议就绪 / FIM 返回空建议 |
| `fim_inline_completion.cpp:115,157` | FIM failed (no details) | FIM 失败（无详情） |
| `fim_inline_completion.cpp:118` | FIM request went stale | FIM 请求已失效 |
| `fim_inline_completion.cpp:136` | FIM request cancelled | FIM 请求已取消 |
| `fim_inline_completion.cpp:160` | FIM unknown state | FIM 未知状态 |
| `fim_inline_completion.cpp:174` | FIM suggestion invalidated (typing continued) | FIM 建议已作废（继续输入） |
| `fim_inline_completion.cpp:185` | FIM suggestion accepted | FIM 已接受建议 |
| `fim_inline_completion.cpp:197,203` | FIM cancelled | FIM 已取消 |
| `src/editor/kernel/fim_wiring.cpp:282` | FIM timed out (%1 ms without a result; cancelled) | FIM 超时（%1 ms 无结果，已取消） |

Note the file the task called `src/editor/fim/fim_wiring.cpp` is really
`src/editor/kernel/fim_wiring.cpp` (registered by `src/editor/CMakeLists.txt:101`);
its line 282 was the only CJK literal there.

`fim_backend.cpp:25` (`the Rust service reported no detail`) and `:178`
(`unavailable (no Rust service in this build)`) were **already** English
`translate("VEdit", …)` sources with catalogue rows — untouched.

### 1.3 Not wrapped (and why)

| literal | where | why it stays |
|---|---|---|
| `简体中文` | `settings_panel.cpp:540` | the language endonym in the language combo — intentionally not translated (named as excluded in the task) |
| `修改` / `无仓库` / `新增` | `vcs_selftest.cpp:4649-4660` | test expectations of the zh catalogue, not product text |
| `sub/中文.v`, `中文注释` | `search_selftest.cpp:148,352,375,379` | test fixtures: a real CJK path/query — the point of the test |
| `用户指南`, `文件(&F)` … | `ux_selftest.cpp:337-408` | test expectations of the zh menu/manual |
| `FIM 未启用`, `FIM 超时（3000ms）`, `未启用` | `fim_selftest.cpp:310,315,351,355,433`, `fim_wiring_selftest.cpp:332` | test inputs/expectations (a fake backend's reply text, and the Rust-supplied text — see F4) |
| `「修复」` in comments | `diagnostic_hover.h`, `diagnostics_integration.cpp`, `diagnostics/README.md` | comments, not strings |

`diagnostic_hover.cpp:112` already reads `translate("VEdit", "Fix")` (changed by
the U2b lane at 10:25, with `hover.fix-button-label-english` asserting it) and the
catalogue already carries `Fix → 修复`; nothing for me to do there.

## 2. Catalogue rows

All 22 were appended to the existing `VEdit` context (the context every
surrounding message in these files already uses — no new context), each with its
real `../src/...` `<location>`s (one row may carry several locations). No
`<source>` of an existing message was modified, no `lupdate` was run, and the
message count went **1186 → 1208** (exactly +22).

## 3. Verification (commands and observed output)

```
$ python3 evidence/ux-batch-2/u2c-final/04-cjk-scan.py        # exit 0
tr()/translate() first arguments containing CJK: 0
CJK string literals left under src/: 24                        # all in §1.3 reasons
```

```
$ /usr/lib/qt6/bin/lrelease translations/vedit_zh_CN.ts -qm /tmp/u2c.qm
Generated 1208 translation(s) (1208 finished and 0 unfinished)   exit=0
$ /usr/lib/qt6/bin/lconvert -i /tmp/u2c.qm -o /tmp/u2c.ts
compiled messages: 1208   round-trip failures: 0 of 22
```
(`05-lrelease-roundtrip.log` prints each of the 22 sources with the translation
that came back out of the compiled `.qm`.)

Syntax of every changed product file, with the exact product flags taken from
`target/qt6-build/compile_commands.json` (`-o/-c` replaced by `-fsyntax-only`, so
nothing is written into the shared tree), run in the pinned image:

```
$ docker run --rm -v "$PWD:/vedit-build" -w /vedit-build vedit-qt6-build:local \
    bash evidence/ux-batch-2/u2c-final/03-syntax-check.sh
(no output, exit 0 — 4 translation units: diagnostics_controller, fim_backend,
 fim_inline_completion, fim_wiring)
```

FIM lane self-test (standalone lane build `target/fim-lane`, rebuilt in the same
image — not the shared `target/qt6-build`):

```
$ docker run --rm -v "$PWD:/vedit-build" -w /vedit-build vedit-qt6-build:local \
    cmake --build target/fim-lane          # clean
$ ./target/fim-lane/vedit-fim-selftest
FIM-SELFTEST ok=1 checks=51 failures=0
```

Substitutions of the two diagnostics strings, measured by loading the compiled
catalogue and calling exactly the strings the product now asks for
(`06-substitution-probe.cpp`):

```
$ /tmp/probe /tmp/probe.qm
PROBE cancelled="已取消 — 120/1000 文件"
PROBE scanned="120/1000 文件已扫描 — 37 条 CLI 诊断"
PROBE substitutions-ok=1
```

**Bound on acceptance (4)**: the probe proves the strings and their `.arg()`
chain, not the `DiagnosticsController` call path. Compiling the diagnostics lane
(`b3_wiring_e2e`, `vedit-diagnostics-*`) needs the full editor/product build,
which the build-tree notice reserves for the captain; the file is syntax-checked
with its real product flags, and the edited expression is only
`QStringLiteral(…)` → `QCoreApplication::translate("VEdit", …)` on the same
`.arg(…).arg(…)` chain.

## 4. Deviation — three self-test expectation literals

The task said not to touch test-expectation literals. Three had to move, because
they assert a string the product now generates — with them unchanged the lane
self-test goes red (measured: `fim.state.no-backend.status` and
`fim.state.text.status` both FAIL before the change, see below):

| file:line | before | after |
|---|---|---|
| `fim_selftest.cpp:302` | `contains(QStringLiteral("未链接 Rust 服务"))` | `contains(QStringLiteral("FIM unavailable (no Rust service linked"))` |
| `fim_selftest.cpp:329` | `contains(QStringLiteral("建议就绪"))` | `contains(QStringLiteral("FIM suggestion ready"))` |
| `fim_wiring_selftest.cpp:456` | `contains(QStringLiteral("超时"))` | `contains(QStringLiteral("FIM timed out"))` |

Same check ids, same conditions, same strictness — only the expected token moved
from the Chinese translation to the English source (the default in a test binary
that installs no translator). Measured before/after on the FIM lane:

```
before: CHECK fim.state.no-backend.status FAIL  FIM unavailable (no Rust service linked; no request was sent)
        CHECK fim.state.text.status FAIL  FIM suggestion ready
        FIM-SELFTEST ok=0 checks=51 failures=2
after : FIM-SELFTEST ok=1 checks=51 failures=0
```

The alternative (install the compiled `.qm` in these tests, as
`vcs_selftest.cpp:4584` does, and keep the Chinese expectations) is possible but
was not taken: the standalone lane build has no `../translations/` next to its
binary, so it would have turned both checks into a SKIP.

`fim_wiring_selftest.cpp:332` (`contains("未启用")`) was **not** touched: it
asserts text that comes from the Rust layer (F4), which my change does not alter.

## 5. Findings not fixed (outside this task)

- **F4 (carried, the same class one layer down) — the Rust FIM service returns
  user-visible Chinese that the Qt catalogue can never reach.**
  `crates/vd-fim/src/error.rs:69-82` builds the error text in Chinese
  (`"FIM 未启用"`, `"FIM 未配置模型"`, `"FIM 超时（{after_ms}ms）"`, …) and
  `service.rs` exposes it as `unavailable_reason()`. `Reply.text` carries it into
  `status_` unchanged (the C++ code only translates its *own* fallback when
  `reply.text` is empty), so the **English UI still shows Chinese for the
  disabled/unconfigured/timeout/transport FIM states**. Measured: the rebuilt
  self-test prints `FIM-HALF-CONFIGURED status="FIM 未配置模型"` and
  `fim.rust.default-off.status-disabled` asserts `未启用` — both from Rust.
  Fix direction (needs a writer of `crates/**`): return a stable code/enum from
  the Rust layer and map it in C++ to `translate("VEdit", …)`, or return English
  and translate in C++. The 20 new FIM rows above are what such a map would use.
- **F5 (locations, carried from T2)** — the catalogue's locations are still
  relative (`../src/…`); the next *full* regeneration must run
  `lupdate -locations absolute -recursive src`. Not done here (it would re-derive
  locations and undo the close-out).
- **F6 (repo hygiene, carried from T2)** — `translations/` is untracked in git
  (`git status --porcelain translations/` → `?? translations/`), so there is no
  `git diff --stat` baseline for it; the captain must stage it explicitly.

## 6. Diff stat of the files this task changed

```
$ git diff --stat -- <the six src files>
 src/editor/fim/fim_backend.cpp                     |   17 +-
 src/editor/fim/fim_inline_completion.cpp           |   45 +-
 src/editor/fim/fim_selftest.cpp                    |    4 +-
 src/editor/kernel/fim_wiring.cpp                   |    5 +-
 src/editor/kernel/fim_wiring_selftest.cpp          |    2 +-
 .../diagnostics/diagnostics_controller.cpp         | 1007 ++++++++++++++++++++
```

The `diagnostics_controller.cpp` figure is the whole batch's diff against HEAD,
not mine: my change is exactly the two lines in §1.1 (that file had already been
rewritten by the diagnostics lane). `translations/vedit_zh_CN.ts` is untracked
(F6) — its real delta is **+22 messages** (+22 `*4` message lines, 1186 → 1208).

Logs in this directory: `03-syntax-check.sh`, `04-cjk-scan.py` + `.log`,
`05-lrelease-roundtrip.log`, `06-substitution-probe.cpp`.
