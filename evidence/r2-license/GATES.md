# Gates and observed results — T6 / r2-license

All commands were run in the workspace root `/home/haroldzhao/MyProj/DshProj/VEdit`
on 2026-10-31 (Asia/Shanghai). Documentation-only change: no source, build script or
packaging file was touched (`git status` below).

## 1. Repository-declared documentation gate

```
$ bash build/gates/docs.sh
[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md
[docs gate exit 0]
```

This is `package.json` → `verify:docs` (`bash build/gates/docs.sh`). **Scope, stated as fact:**
it checks exactly four fixed files — `README.md`, `docs/BUILD.md`, `docs/USER_GUIDE.md`,
`THIRD_PARTY_NOTICES.md` — and requires each to have ≥10 non-empty lines. It does **not** walk
`docs/**`: `docs/decisions/` (including `docs/decisions/r2-license.md`) is **outside the gate** and
is not validated by any automatic check. (Suggestion for the captain, not implemented here: if ADR
coverage is wanted, `build/gates/docs.sh` would have to be extended deliberately — this task was
forbidden from changing gate thresholds.)

## 2. Evidence self-check (the report's own claims)

```
$ bash evidence/r2-license/verify.sh
...
== summary: 51 passed, 0 failed ==
ALL ASSERTIONS PASSED
[exit 0]
```

Full output: `evidence/r2-license/verify.log`.

Note on honesty: the **first** run was `43 passed, 1 failed` — the failing assertion used the
literal `version = "0.4.6"`, while `lapce/Cargo.toml:26` is aligned as
`version      = "0.4.6"`. The assertion was replaced by the equivalent anchored regex
`^version[[:space:]]*=[[:space:]]*"0\.4\.6"`, which still asserts the same fact. No assertion
was dropped and no expected value was relaxed.

## 3. Cited-link resolution

```
$ grep -oE 'https?://[^ )`"]+' evidence/r2-license/report.md docs/decisions/r2-license.md \
    | sort -u | while read -r u; do curl -sSL -o /dev/null -w '%{http_code} %{url_effective}\n' "$u"; done
200 https://api.github.com/repos/lapce/lapce
200 https://api.github.com/repos/lapce/tree-sitter-grammars/releases?per_page=100
200 https://api.github.com/repos/lapce/tree-sitter-grammars/tags?per_page=100
200 https://download.gnome.org/sources/gtksourceview/4.6/gtksourceview-4.6.1.tar.xz
200 https://github.com/helix-editor/helix
200 https://github.com/lapce/lapce
200 https://github.com/tree-sitter/tree-sitter-javascript
200 https://github.com/tree-sitter/tree-sitter-verilog
200 https://raw.githubusercontent.com/lapce/lapce/b012cef466741528f338879fa708355217fc40bd/Cargo.lock
```

Saved as `evidence/r2-license/link-check.log` (0 non-200).

## 4. Write scope

```
$ git status --porcelain=v1
?? docs/decisions/
?? evidence/github/readiness.md     <- pre-existing, not mine
?? evidence/r2-license/             <- T6
?? probes/                          <- pre-existing, not mine
```

New files created by this task: `evidence/r2-license/{report.md,verify.sh,verify.log,link-check.log,GATES.md}`
and `docs/decisions/r2-license.md`. Nothing else was written.

## 5. Why the heavy Rust gates were not run

`AGENT.md §4` declares `bash scripts/build.sh`, `cargo test --workspace`, clippy and fmt for the
**current GTK3/Rust product**. This task added two markdown files and one evidence shell script and
touched no Rust source, no `Cargo.toml`/`Cargo.lock`, no build script and no packaging file, so
those gates cannot be reddened by this change (`git status` above is the proof). The relevant
declared gate for the changed paths — the documentation gate — was run and passed (§1). No test or
gate file was modified.

## 6. Shell syntax of the new script

```
$ bash -n evidence/r2-license/verify.sh
syntax OK
```

## 7. T54 re-run（D2 收口后，2026-10-31）

**裁决来源**：用户裁决经 captain 在任务 `T54` 转达（`.mpd/team/contracts/T54.json`；journal
`dispatch` 事件 `2026-10-31T02:47:01.202Z`）。**这是转达**——用户原话不在本会话中，故按
「captain 转达」记录，不冒充直读。

**T54 改动的文件**：
- `docs/decisions/r2-license.md` —— D2 → **Accepted(B)** + 6 条执行口径；§5 断言扩为 A/B（永久禁止
  KDE 定义）、C（正向要求自有 LGPL `.sv` 定义，落地前保持 RED）、D（映射不得回退）、E（grammar
  LICENSE 锁定）、F（自有 `folds.scm`）；§6 未闭合项重写；§7 新增转交清单（T16/T38/T17）。
- `evidence/r2-license/report.md` —— §0.4 处置标注为已裁决、新增 §0.5 裁决记录、§5.2 行 B 标为
  v1 默认、§6 更新 U3/U5/U7 并新增 U9（T16 契约文本对齐）；`assets/language-specs/` 措辞收紧为
  「该路径**没有** `.lang` 文件，必须从 tarball 取」。

**改动后的门禁**：

```
$ bash build/gates/docs.sh
[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md
[docs gate exit 0]

$ bash evidence/r2-license/verify.sh
== summary: 44 passed, 0 failed ==
ALL ASSERTIONS PASSED
[verify exit 0]
```

未触碰生产代码、构建脚本、打包文件、门禁阈值；**未**改 `requirements.md`（未升 revision），按
captain 指令执行。

## 8. T55 re-run（修 T6 评审 M1 + N1–N4，2026-10-31）

**评审来源**：T6 盲评 `rec-20261031T025011-f22e32`（结论 approve，1 Medium + 4 Nit），经 captain
于 `T55` 转达。**这是转达**——评审记录本身不在本会话中。

| 项 | 处置 | 落点 |
|---|---|---|
| **M1**（Medium）证据覆盖 | `verify.sh` **新增真拉取断言**：`Cargo.lock`（193,528 B，sha256 `ab95631c…`）+ `^name = "tree-sitter` 计数 == 1 + 两个 grammar crate 名缺席；另加 **3 条负控**证明这些断言可失败（合成 2-grammar 夹具计数=2；含 grammar 名的夹具被缺席检测命中；干净夹具不误报）。报告 §7 的覆盖声明改为逐项事实，并更正「两个 MIT 候选」→「1 个 MIT grammar 候选 + Verible 作为 Apache 路线的非 tree-sitter 结构性替代」 | `verify.sh`、`report.md` §7 |
| **N1** | §0.1「能，无条件」→「**能，附 MIT 通知保留义务**」（引 `LICENSE:12-13`，指向 §4 第 6 条） | `report.md` §0.1 |
| **N2** | GATES §1 覆盖声明改为事实：docs gate 只查 4 个固定文件、≥10 非空行；**`docs/decisions/` 不在门禁范围**；扩范围建议只上报 captain，未自行实施 | `GATES.md` §1 |
| **N3** | E7 补 `main.rs` 的 raw URL；E9 补仓库 URL + 三个文件的 raw 前缀；E10 补 `grammar.js` URL | `report.md` E7/E9/E10 |
| **N4** | §3 第 3 条标为「**法律判断，需法务确认**」，并按最保守口径表述 | `report.md` §3 |

**重跑观测**（本次）：

```
$ bash evidence/r2-license/verify.sh        # 51 条断言（含 3 条负控）
...
== summary: 51 passed, 0 failed ==
ALL ASSERTIONS PASSED
[verify exit 0]

$ bash build/gates/docs.sh
[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md
[docs gate exit 0]
```

行数变化：断言 **44 → 51**（+4：`Cargo.lock` sha256 / 计数 / 2 条缺席；+3：负控）。日志已刷新到
`evidence/r2-license/verify.log`。未触碰生产代码/构建/打包/门禁阈值；未改 `requirements.md`；
未改 `docs/decisions/r2-license.md`（其 D2 口径属 T54）。

**过程披露（不藏失败）**：本次重跑第一次以 `50 passed, 1 failed` 退出——失败项是
`curl-error .../tree-sitter-verilog/4457145.../LICENSE`，属**网络瞬时失败**，不是断言不成立。
处置：给 `fetch()` 加了 4 次重试（`--retry 2` + 退避），随后复跑得 `51 passed, 0 failed`。
这是把「网络抖动」与「证据不符」分开，**没有**放宽任何期望值——同一份 `verify.log` 在重跑中
所有 sha256 与原文断言均逐条通过。
