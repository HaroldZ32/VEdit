# 独立评审 verdict — T5 rev-ffi：Slice 0 Rust 服务层 + C ABI 纵向切片（ffi-seam）

- Cell: `cell-20261031T085543161Z` · 评审任务: `T5`（rev-ffi）· 被评 work: `T4`（ffi-seam）
- 冻结契约: `.mpd/cellwork/cell-20261031T085543161Z/contracts/ffi-seam.md`（work）+ `contracts/rev-ffi.md`（本评审）
- 验证 loop: `loop-20261031T095153-01121e`（已绑定 seat；verdict 记录 `rec-20261031T103517-f665a8`，PASS）
- gate evidence id: `ev-20261031T102717-505736`（`bun run typecheck`；其红/绿两半的说明见 §4）
- 评审人: **Reviewer**（盲审：先读契约 + 文档 + 证据，再跑门禁，**先记录 verdict**；未读 Rust/C++ 实现源码）
- 评审时刻: 2026-10-31 18:29–18:37（Asia/Shanghai）。评审开始 HEAD `9957f5f`，工作树含该 work 的未提交改动；评审期间出现多次 checkpoint 提交（`f349342`、`3cdbbe9`、`0904a1d`、`2808280` 等）与其他 lane 的并发改动/并发构建（详见 §5）
- 环境: 宿主 Linux 7.0.0-38-generic x86_64；镜像 `vedit-build:local`（CentOS 7.9 / rustc 1.99.0）与 `vedit-qt6-build:local`（Ubuntu 26.04.1 / Qt 6.10.2 / KF6 6.24.0）
- 风险等级: **Medium**
- 结论: **PASS（附带 findings）** — 契约逐条成立；独立复跑的门禁为绿；findings 全部不阻塞，集中在门禁基础设施与证据交付。

---

## 1. 总体评估

这是一条**真正纵向打通**的切片：`crates/vd-ffi` 用 cbindgen 从 `extern "C"` 面生成头文件，头里只有 `typedef struct VdService VdService;` 这样的 opaque handle 与 C POD（`int32_t/uint32_t/char*/嵌套 struct`），把七项服务（工程模型+`.f`、LSP、deeplint、FIM、search、VCS、settings）的入口一次固化，统一以 `uint64_t` job id + poll 的异步形态返回（不阻塞调用方）；`crates/vd-search`/`vd-settings`/`vd-vcs` 三个后端 crate 注册进 workspace 并各带单测；`cmake/VEditRustFfi.cmake` + 根/应用 CMake 把 `libvd_ffi.a` 静态链进 `vedit-app`（我在 `build.ninja` 的链接行里看到 `/vedit-build/target/release/libvd_ffi.a`），`--selfcheck-ffi` 作为 ctest 第 6 项在真实 C++ 侧经 C ABI 解析 fixture `.f` 得到预期文件列表；证伪（改坏 ABI 面 → 构建红；恢复 → 全绿）有前后原始日志。我按 C9 的“附 target 清单 + 一次证伪”逐条核过：target 清单（`22-rust-targets.txt`）、静态库（`21-staticlib.txt`，56 MB）、28 个 C ABI 导出符号、四 crate 30 个单测、`vedit-app` 里 20 条 ABI CHECK——都能在证据或我自己的运行里对上。

我自己**独立复跑**了门禁并得到绿：`bash scripts/build-qt.sh` exit 0、ctest **10/10**（含 `vedit-app-ffi-abi-selftest`）与 xcb 软件渲染冒烟；在 `vedit-build:local` 里 `cargo fmt --check` / `cargo clippy --workspace --all-targets -- -D warnings` / `cargo test -p vd-ffi -p vd-search -p vd-settings -p vd-vcs` 全部 exit 0；`cargo tree -p vd-ffi` 里 gtk/sourceview4/vd-editor/vd-syntax 命中 **0**；`Cargo.lock` 无 `tree-sitter*`/`openssl`/`native-tls`；头文件在剥离注释后无 Rust 类型；workspace lint 与 HEAD 逐字节相同、`unsafe` 只出现在 `vd-ffi`。唯一“红”的是 ledger 的 `typecheck` id：它的 Rust 半段绑定在宿主 `cargo` 上，而宿主 1.99.0 toolchain 缺 `cargo/clippy` 组件——这是**门禁基础设施**问题（M1），不是本 work 的缺陷；同一份门禁日志的 Qt6/Rust-FFI 半段是 `[qt6] PASS`，我也在镜像里重跑了等价的 Rust 命令（全绿）。

## 2. 逐条核对（contract → 证据/我的命令）

| 条款 | 结论 | 我据以判断的证据 / 我的观察 |
|---|---|---|
| **AC-0.1 架构边界可证** | ✅ | `target/qt6-build/build.ninja` 的 `vedit-app` 链接行含 `/vedit-build/target/release/libvd_ffi.a`；`nm -C vedit-app \| grep -E " T vd_"` → **28** 个 C ABI 符号（`vd_project_parse`、`vd_lsp_start`、`vd_deeplint_lint`、`vd_fim_complete`、`vd_search_query`、`vd_vcs_status`、`vd_settings_load/save` + 公共 6 个）；证伪 `logs/40-falsification-red.log`（`FALSIFY-EXIT=1`，真实编译错误：`main.cpp:332: error: 'vd_abi_version' was not declared`）与 `logs/41-falsification-restored.log`（`RESTORED-EXIT=0`，10/10） |
| **AC-0.2 七项服务各 C ABI 入口 + 单测；Qt 侧无第二套实现** | ✅ | 生成头 `crates/vd-ffi/include/vd_ffi.h`（与 `logs/26-vd_ffi.h` sha256 同为 `26caba1b…`）声明了七族入口；`logs/23-cargo-test.log` 的 vd-ffi 12 例含 `projected_fixture_serves_all_seven_services` 与逐服务用例；`src/app` 的 work 增量只有 `main.cpp` + `CMakeLists.txt`（`git diff --stat -- src/app` 在工作树对 HEAD 已 clean），没有新增 C++ 服务实现文件 |
| **AC-0.3 GTK 隔离** | ✅ | `cargo tree -p vd-ffi \| grep -cE "gtk\|sourceview4\|vd-editor\|vd-syntax"` → **0**；`52-reuse-boundary.txt` 同结论 |
| **AC-0.4 unsafe 口子受控** | ✅ | `git diff Cargo.toml` 只有 workspace members 增加；`[workspace.lints]` 段与 `HEAD:Cargo.toml` 逐字节相同（我 diff 过）；`grep -rnE "\bunsafe\b" crates \| grep -v crates/vd-ffi` → 无；仅 `crates/vd-ffi/Cargo.toml` `[lints.rust] unsafe_code="allow"` |
| **AC-0.5 验证分栏** | ✅ | `53-verification-split.txt` 把每条证据标成 agent/user，且明确“只用户可做”的项（真实窗口手感、四宿主 AppImage、七项服务语义的产品取舍）**未被声称** |
| **AC-S0.1 构建接线** | ✅ | 我独立跑 `bash scripts/build-qt.sh` → exit 0；ctest `100% tests passed, 0 tests failed out of 10`；`logs/22-rust-targets.txt` 是真实编译清单（含 `vd_ffi`/`vd_config`/`vd_lsp`/`vd_search`/`vd_settings`/`vd_vcs`/`vd_deeplint`/`vd_fim`）；`logs/21-staticlib.txt` = `target/release/libvd_ffi.a` 56,102,610 B |
| **AC-S0.2 ABI 冒烟** | ✅ | 我独立跑了 `vedit-app --selfcheck-ffi crates/vd-ffi/tests/fixtures/project` → `FFI-ABI-SELFTEST ok=1 checks=20 failures=0`，exit 0；fixture 的 `rtl/files.f` 覆盖 `+incdir+inc`、`+define+WIDTH=8`、`+define+SIM`、`-f nested.f` 与 `//`+`#` 注释，`nested.f` 再列 `rtl_c.sv`；断言覆盖 file-count/order/include-dir/defines |
| **AC-S0.3 证伪** | ✅（读证据，未复跑） | `logs/40` 红（exit 1，编译期缺 `vd_abi_version` 声明）/ `logs/41` 绿（exit 0，10/10）——红是“真红”（编译错误，不是脚本失败），符合 AC-S0.3 的“构建变红/恢复变绿”；未复跑的原因见 §5 |
| **AC-S0.5 复用边界** | ✅ | `cargo tree` 含 `vd-config`/`vd-lsp`/`vd-deeplint`/`vd-fim`（`logs/25-reused-crates.txt` + 我的 `cargo tree`），不含 `vd-editor`/`vd-syntax` |
| **C1 架构分层（Qt=UI，Rust=服务）** | ✅（有界） | work 的写域与 porcelain 显示 C++ 侧只新增“调用方”（`main.cpp` 的 `ffi::runSelfCheck` + ctest 注册）；七项服务全部经 ABI 由 Rust crate 提供（`cargo tree` + 头文件）；**“不存在第二套实现”是负向命题**，盲审下我只能以“改动面 + 依赖树”支持，不能证明绝对不存在（见 §5） |
| **C2 接口形态（C ABI / opaque+POD / 头无 Rust 类型 / 不阻塞）** | ✅（有界） | 头：`typedef struct VdService VdService;` opaque；结构体只有 `int32_t/uint32_t/char*/嵌套 struct`；剥离注释后 Rust 类型 token = 0；入口返回 `uint64_t` job + `*_poll`，selftest `ffi.project.start.non-blocking` 且 `FFI-TIMING project-start-ms=0`；**只有 project-start 有计时断言**（其余服务“非阻塞”由设计保证，未逐项计时） |
| **C3 复用边界 / 唯一 unsafe crate** | ✅ | 同上 + `crates/vd-ffi` 是唯一 `unsafe_code="allow"`；vd-config/vd-lsp/vd-deeplint/vd-fim 被复用（`logs/25`） |
| **C5 构建（pinned 镜像内 build-qt.sh；含 Rust target 清单 + FFI 产物；glibc 不回归）** | ✅ | 我跑 `build-qt.sh` 的日志打印 Rust 层 target 清单与 `libvd_ffi.a`；configure 打印 `-- VEdit: Rust service layer vd-ffi -> …/libvd_ffi.a`、`Rust C header -> …/vd_ffi.h`、`vedit-app links the Rust service layer (vd-ffi); ABI selftest registered`；Rust 二进制的最高 GLIBC 符号 = `GLIBC_2.16`（`51-glibc-floor.txt`），Qt 产品自身的 floor 由私有运行时满足（文档已登记） |

## 3. Findings（按严重度）

### M1 [Medium — 门禁基础设施，不是本 work 的缺陷] ledger 的 `typecheck` id 在本宿主上必红：Rust 半段绑定宿主 `cargo`，而宿主 pinned toolchain 缺该组件
- 是什么：`mpd_verify_evidence gate:typecheck`（`ev-20261031T102717-505736`）exit **1**。日志前半是 `[qt6] PASS: Rust service layer + configure + build (warnings-as-errors) inside vedit-qt6-build:local`；后半 `[typecheck] clippy 未安装：声明式退化为 cargo check --workspace` 随后 `error: the 'cargo' binary ... is not applicable to the '1.99.0-x86_64-unknown-linux-gnu' toolchain`。根因是 `build/gates/typecheck.sh` 的 Rust 段直接在宿主执行 `cargo`/`cargo clippy`，而宿主的 rustup 只装了 1.99.0 的 `rustc`（`cargo --version` 不可用），与本次改动无关（`build/gates/typecheck.sh` 不在本 work 的写域里，未被改）。
- 为什么重要：在这个宿主上，**任何** Rust 改动都无法用 ledger 的 `typecheck` 拿到绿；验证者只能去镜像里手工跑等价命令（我做了，见 §4），这让“ledger 绿”与“事实绿”脱节——正是本项目反复吃过的“假绿/假红”类问题。work 的 `54-gates.txt` 已诚实披露了同类 caveat（`cargo test --workspace` 需要先建 slang artifact），但没有覆盖宿主缺 cargo 这一条。
- 修复方向（不实施）：把 `build/gates/typecheck.sh` 的 Rust 段改成与 `scripts/gates.sh` 相同的“宿主缺 GTK/cargo 时进 pinned 镜像”的显式委派，并打印委派行；或给宿主镜像补齐 pinned toolchain 的 `cargo`/`clippy` 组件。**不要**为了让 gate 变绿而降低检查。
- 证据：`.mpd/verify/evidence/ev-20261031T102717-505736.log`；`build/gates/typecheck.sh` 尾部（`exec cargo check --workspace`）；`docs/BUILD.md` §12.5；`docs/handover/05-build-and-verify.md` §4；`AGENT.md` §4。

### M2 [Medium — 证据/交付纪律] work 的证据目录在评审时未入库（现已由 checkpoint 补上），但 C9 的 HEAD 戳已过期，且门禁日志在被并发重写
- 是什么：评审/记录时刻（18:35 前后）`evidence/ffi-abi/` 仍是 **untracked**（`git status --porcelain -- evidence/ffi-abi` = `?? evidence/ffi-abi/`）；随后 captain 在 18:36:07 用 `0904a1d`（`chore(ffi): checkpoint slice 0 ffi abi evidence and qt6 build logs`）把它入库——这一半已解决。仍有两件事没解决：① `00-collect.txt` 记的 `HEAD=9957f5f` 早于 work 自身的 checkpoint 提交（`f349342`/`3cdbbe9`/`0904a1d`），证据与“当前 HEAD”不再一一对应；② 门禁日志是易失的——我的 `build-qt.sh` 复跑与**其他成员的并发构建**在 18:31、18:37 反复重写 `logs/19–23`（现在相对 `0904a1d` 显示 `M`）。
- 为什么重要：C9 要求“在冻结的树上重采证据”，AGENT.md §7 要求证据可被外人重跑；如果 PR 直接带着“被后续构建改过的日志 + 旧 HEAD 戳”发布，证据的树状态与时间就不再自洽，下一个评审者无法判断哪一版日志对应哪个提交。
- 修复方向：在 PR 前**冻结树**（停掉并发构建），按 C9 重采一次 `evidence/ffi-abi/`（`00-collect.txt` 写新的 HEAD + porcelain），并把重采结果整体提交；不要在移动的树上补交单条日志。
- 证据：`git status --porcelain -- evidence/ffi-abi`；`git log -1 --format='%ci %s' -- evidence/ffi-abi`（`0904a1d` 18:36:07）；`git log --oneline -3 -- crates/vd-ffi`；`evidence/ffi-abi/00-collect.txt`；`ls -la evidence/ffi-abi/logs/19-…`（18:37:29 的并发重写）。

### N1 [Nit] Rust 内部符号随静态库进入 `vedit-app` 的符号表
- 是什么：`nm -C vedit-app | grep -E " T vd_"` 共 58 条，其中 **28** 条是 C ABI，其余 30 条是 demangle 后的 Rust 内部符号（`vd_config::…`、`vd_lsp::…`、`vd_ffi::capi::…`）。头文件没有暴露它们，契约也未要求隐藏；只是可执行文件符号面比预期大。
- 方向：若将来在意符号面/冲突，用 `-Wl,--exclude-libs,ALL` 或 version script 只导出 `vd_*` C ABI。不改判据。

### N2 [Nit] 头文件 include guard 拼写 `VDIT_VD_FFI_H`
- 是什么：`crates/vd-ffi/include/vd_ffi.h` 的 guard 是 `VDIT_VD_FFI_H`（VEDIT 少了 E）。无功能影响，但会被后来者当成“生成器配置有 typo”的线索。
- 方向：cbindgen 配置里改 guard 名即可。

### N3 [Nit] C++ 边界冒烟覆盖 4/7 项服务
- 是什么：`--selfcheck-ffi` 的 20 条 CHECK 覆盖 project/fim/vcs/settings（`logs/30-ffi-selftest.log`）；lsp/deeplint/search 只在 Rust 单测里（`logs/23` 有逐服务 typed-failure 用例），没有 C++ 侧的 C ABI 冒烟。
- 为什么提：AC-0.2 只要求“入口 + 单测”，已满足；但“C++ 真的经 ABI 用到这七项”目前只对四项成立。不是失败，是覆盖缺口。
- 方向：给 lsp/deeplint/search 各加一条 C ABI 冒烟（它们本来就有 typed-failure 用例，几乎零成本）。

## 4. 我实际复跑的（命令 + 观察，原文可复跑）

```sh
# 1) 权威构建门禁（Slice 0 的 AC-S0.1 命令；锁定镜像内）
bash scripts/build-qt.sh
# 观察：BUILD-QT-EXIT=0；100% tests passed, 0 tests failed out of 10（含
#       vedit-app-ffi-abi-selftest）；M0-SMOKE ok=1（xcb + llvmpipe）；
#       "[qt6] PASS: Rust service layer + configure + build + ctest (incl. the C ABI smoke)
#        + xcb/software-rendering smoke inside vedit-qt6-build:local"

# 2) Rust 编译门禁（vedit-build:local，pinned）
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-build:local bash -lc '
  cargo fmt --all -- --check                                   # exit 0
  cargo clippy --workspace --all-targets -- -D warnings        # exit 0
  cargo test -p vd-ffi -p vd-search -p vd-settings -p vd-vcs   # exit 0
  cargo tree -p vd-ffi | grep -cE "gtk|sourceview4|vd-editor|vd-syntax"  # 0
  grep -nE "^name = \"(tree-sitter[^\"]*|openssl|openssl-sys|native-tls)\"$" Cargo.lock'
# 观察：test result: 12 + 6 + 6 + 6 passed, 0 failed；无 forbidden crate

# 3) ledger 门禁（记录为 evidence id）
#    mpd_verify_evidence {kind:"gate", gate:"typecheck"} -> ev-20261031T102717-505736, exit 1
#    红的原因只在宿主 Rust 半段；同日志的 FFI/Qt 半段是 [qt6] PASS（见 M1）

# 4) C ABI 冒烟的独立运行
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local bash -lc \
  'QT_QPA_PLATFORM=offscreen target/qt6-build/src/app/vedit-app \
     --selfcheck-ffi /vedit-build/crates/vd-ffi/tests/fixtures/project'
# 观察：FFI-ABI-SELFTEST ok=1 checks=20 failures=0；SELFTEST-EXIT=0

# 5) 链接/符号/头文件
grep -c 'libvd_ffi.a' target/qt6-build/build.ninja            # 2（链接行 + LINK_LIBRARIES）
nm -C target/qt6-build/src/app/vedit-app | grep -E ' T vd_'    # 28 条 C ABI + 30 条 Rust 内部
sha256sum crates/vd-ffi/include/vd_ffi.h evidence/ffi-abi/logs/26-vd_ffi.h
# 观察：两者同为 26caba1b977e6498d6983df7e2496d59b2e034ee9ab8b94a47c63082892b3271
```

门禁结论：本 work 实质要求的门禁（`scripts/build-qt.sh`、pinned 镜像内的 fmt/clippy/目标 crate 测试）**全部绿**；ledger 的 `typecheck` id 因宿主缺 `cargo` 组件为红（M1，基础设施），`gates` id 因 `build/slang/out/slang` 缺失需要先建 slang 而未跑（同属既有基础设施缺口）。

## 5. 我不判的（declined to judge，逐条给理由）

1. **AC-S0.4（打包含 Rust / AppImage 内含 FFI）** — work 契约的验收清单**没有**列 AC-S0.4（列的是 AC-0.1..0.5、AC-S0.1/0.2/0.3/0.5）；打包属 pack-final lane。不在本评审范围。
2. **证伪的独立复跑** — 复现需要改坏 `src/app`/头文件再恢复，评审期间工作树有多个成员在写（且 work 已被 checkpoint 提交），改坏-恢复会制造并发冲突；改为核对红/绿日志的**具体性**（`logs/40` 是编译期未声明符号，不是脚本失败）与恢复后的 10/10。
3. **`cargo test --workspace` 全量** — 已知红：`crates/vd-deeplint/tests/real_slang.rs` 需要 `build/slang/out/slang`，该 artifact 本机不存在且不在本 work 写域（`54-gates.txt` 已披露）；我只跑目标四 crate。
4. **“Qt 侧不存在第二套实现”的绝对证明** — 盲审下不读 C++/Rust 实现；只能以“work 改动面（`src/app` 仅 main.cpp+CMakeLists）+ 依赖树（七项服务全在 Rust crate）+ C++ 侧只调用 ABI”支持，属于有界结论。
5. **真实外部依赖的语义**：FIM 真实端点、Verible 服务端、slang 二进制、git/svn 的真实行为 — 都是各服务后续 lane 的产品行为，不在 Slice 0 的“接缝形状”验收内。
6. **`libvd_ffi.a` 内的 Rust std 对最终 ELF floor 的净影响** — 我核到的 Rust 二进制 floor 是 GLIBC_2.16，Qt 产品 floor 由私有运行时供给（文档已登记）；逐符号核算 vedit-app 的净 floor 属打包 lane。
7. **`vd-config`/`vd-lsp` 的并发改动** — 评审期间其他 lane 在改这两个 crate 的源码（`git status` 可见）；我的绿色门禁运行发生在这些改动出现的时间窗内，但 FFI work 自身路径 relative to HEAD 是 clean 的。它们的正确性属各自 lane 的评审。

## 6. 本次评审的边界

- **盲审**：先读契约/文档/`evidence/ffi-abi/`/生成的头文件（ABI 契约面）与构建产物，**先记录 verdict**（`rec-20261031T103517-f665a8`），未读 `crates/*/src` 与 `src/app/*.cpp` 实现。
- 没有跑 ledger 的 `gates`（需要从源码构建 slang，时间/网络不成比例），没有跑全量 workspace 测试（见 §5.3），没有复跑证伪（§5.2）。
- 没有在有屏环境验证交互（本 work 不涉及 UI 交互）。
- 我的 `build-qt.sh` 复跑按该脚本的既有行为刷新了 `evidence/ffi-abi/logs/*` 与 `evidence/m0-build/build-logs/{31,32,33,34,36,37}-*`（gate 的输出路径，非我手写）；`00-collect.txt`、`README.md` 与 `50–54` 文件未被覆盖。评审期间其他成员也在跑构建，这些日志在 18:37 仍在被重写（M2 的第二半）。
- 只写入了 `evidence/verdicts/ffi/verdict.md`（本文件）；验证记录由 `mpd_verify_record` 写入 `.mpd/verify/`。

## 7. 结论与一句话理由

**Verdict: PASS（附带 findings）** — 风险 Medium。

理由：ffi-seam 的冻结契约（AC-0.1..0.5、AC-S0.1/S0.2/S0.3/S0.5、C1–C5）逐条成立，且最关键的构建接线与 ABI 冒烟由我独立复跑为绿（`build-qt.sh` exit 0、ctest 10/10、镜像内 fmt/clippy/30 单测全绿、`--selfcheck-ffi` 20/20、28 个 C ABI 符号真实链接进 `vedit-app`）；两条 Medium 分别是**门禁基础设施**（宿主 `typecheck` 的 Rust 半段必红）与**证据未入库/HEAD 戳过期**，都不推翻本 work 的验收，但必须在 PR 前处理掉。
