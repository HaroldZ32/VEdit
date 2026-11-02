# T27 — FIM：默认关闭的零网络证明 + 配置后经 Rust 服务可用

generated: 2026-11-01T07:41:23Z
collected_at: 2026-11-01T15:41:23+0800   # T105 evidence repair snapshot (backfill), see the binding section
collected_at_utc: 2026-11-01T07:41:23Z
collected_at_note: 本文件在补记快照之后写入磁盘（mtime 晚于 collected_at）；日志头里的 backfill_snapshot 与本值一致。
backfill: 补记（T105）—— 本报告的头部时间/HEAD/porcelain 是**补记时刻**的快照；原始 T27 采集时间见下。
  原始 REPORT 的 mtime 是 2026-11-01T15:20:45+08:00，而它当时写的 collected_at 是 15:22:00 ——
  自相矛盾（报告时间晚于自身 mtime）。该值作废，不沿用；本报告的 collected_at 是本次补记的真实快照时间。
git_head: d36bff9470dca6bb94ee6c5529c7fb5328f344c9
git_status_porcelain:
  M build/evidence/typecheck/00-run.txt
  M build/evidence/typecheck/10-typecheck-image-build.log
  M build/evidence/typecheck/11-typecheck-image-id.txt
  M crates/vd-ffi/include/vd_ffi.h
  M crates/vd-ffi/src/fim.rs
  M crates/vd-ffi/src/service.rs
  M evidence/ffi-abi/logs/10-rust-image.txt
  M evidence/ffi-abi/logs/19-rust-stage-console.log
  M evidence/ffi-abi/logs/20-cargo-build-release.log
  M evidence/ffi-abi/logs/21-staticlib.txt
  M evidence/ffi-abi/logs/23-cargo-test.log
  M evidence/ffi-abi/logs/26-vd_ffi.h
  M evidence/ffi-abi/logs/27-vd_ffi-comment-free.txt
  M evidence/m0-build/build-logs/20-qt6-image-build.log
  M evidence/m0-build/build-logs/21-qt6-image-id.txt
  M evidence/m0-build/build-logs/31-qt6-configure.log
  M evidence/m0-build/build-logs/32-qt6-build.log
  M evidence/m0-build/build-logs/33-qt6-ctest.log
  M evidence/m0-build/build-logs/34-qt6-elf.txt
  M evidence/m0-build/build-logs/37-qt6-rust-abi.txt
  M src/editor/CMakeLists.txt
  M src/editor/kernel/ktexteditor_provider.cpp
  M src/workspace/palette/README.md
  M src/workspace/search/CMakeLists.txt
  M src/workspace/search/README.md
  M src/workspace/search/global_search.cpp
  M src/workspace/search/global_search.h
 ?? evidence/fim-wiring/
 ?? evidence/slice1-stop/61-typecheck-slice3.log
 ?? evidence/slice1-stop/62-build-qt-slice3.log
 ?? evidence/slice1-stop/70-pre-run-fim.txt
 ?? evidence/slice1-stop/71-typecheck-fim.log
 ?? evidence/slice1-stop/72-build-qt-fim.log
 ?? evidence/slice3-fim/
 ?? evidence/verdicts/fim/
 ?? src/editor/fim/
 ?? src/editor/kernel/fim_wiring.cpp
 ?? src/editor/kernel/fim_wiring.h
 ?? src/editor/kernel/fim_wiring_selftest.cpp
git_status_porcelain_sha256: 08b7a6f9fd5b3a343cdf9e3e0304f0483db66db91442ad90f9839557f6a56488   # sha256 of exactly the porcelain block above
---
（提交由 Lead 统一负责；本任务未 commit、未 push。）

无头声明：本机没有显示服务器；所有证据都是 offscreen/进程内可复跑的。真实模型端点**没有**被访问，
也不作为门禁。


## Evidence binding（T105 补记）

### 两个 HEAD 的来历（不是两个产品）

T27 采集期间，Lead 的提交 **`d36bff9`**（"docs(evidence): land the slice 2/3 evidence …"）在
采集进行到一半时落地。因此日志里存在两个 HEAD 值；它们指向**同一工作树内容**（本任务代码未变），
只是记录时刻不同：

| HEAD | 完整 sha | 出现在 |
|---|---|---|
| A | `48569602d0c86fb402dc88155593d1dc06445c90` | `01`(15:12:51)、`02`(15:13:21)、`06-lane-ctest`/`06-root-ctest`(15:15:26)、`06b`/`06-typecheck`(15:21:34)、`04`(15:21:50)、`05`(15:21:51) —— 各日志头里记录的采集时刻 |
| B | `d36bff9470dca6bb94ee6c5529c7fb5328f344c9` | `03-lane-build`(15:23:14)、`04b`(15:23:13) —— 提交落地之后 |

- 提交落点在 **15:21:51（最后一条 HEAD A 记录）与 15:23:13（第一条 HEAD B 记录）之间**，以上时间均为
  日志头里 T27 采集时写下的原值（本次补记只补 porcelain，不改这些值）。
- `00-run-evidence.log` 与 `repro/*` 在采集时**没有写头**，因此其 HEAD 标为 `unknown`（不推断）；
  它们运行于 15:16–15:19，前后相邻日志记录为 HEAD A，可作上下文但不是记录值。
- **被测文件 sha256**（二进制在采集后又被后续构建覆盖过，因此同时列出“采集时记录值”和“当前值”）：

| 产物 | sha256 |
|---|---|
| lane binary (recorded in 04/05) | `adef16c710ce9bfaedde04e45240b3409b84341fff7f9880b7049896c3a35e0a` |
| lane binary (current, after later rebuilds) | `c2d73e642f8e5c345953ed5f67d3cf3223aeb7aa3af1e95960c2e87331b01264` |
| product vedit-app (recorded in 04b) | `63a27a532b376cab0ae285a0797c6cacd55cf3e6cd014ed1e69571c54a16bb42` |
| product vedit-app (current) | `61e19acda47fe2c9e2e34841cd292d93b3dbc90281c23fce93f89ba4f6f8d402` |
| libvd_ffi.a (current) | `2ad5b187ae3064b31f4b5c95d96f7fa6233db7c6779c30b51c9eb996d1bf47ee` |
| crates/vd-ffi/src/fim.rs (measured worktree) | `1ca08772b0079458affbb9a39d16eabe5b0aca6839106f4b5f160f3c0f09b4f8` |
| crates/vd-ffi/src/fim.rs (committed at HEAD d36bff9) | `070c849caffd4ae76ac2448b5f4171552b241ec402d37a8e60310ff98a588064` |
| src/editor/fim/fim_backend.cpp | `d3cbd1def0c679c70f355ed37ee6460deee932c82df32cf0e1c23ade16137988` |
| src/editor/fim/fim_inline_completion.cpp | `396f1c68be59101c74e69c54dab75e06e79839d09f07539419c9b6056b368285` |
| src/editor/fim/fim_selftest.cpp | `7693de25c1af39e70718fb471f9bee38b85519a8f98e4b4878a60c0e94276108` |
| src/editor/fim/CMakeLists.txt | `1c208e28bd16f8de2ac3d8d3dee9b745b4a9fb5d7bd2a7158c558689855668da` |
| evidence/slice3-fim/socket_probe.c | `48bed864e14054d4ad3e9c90e1f0bc865253c4a5bd3bcdaa9721af63aef28c58` |
| evidence/slice3-fim/run-evidence.sh | `86eec25af45122a8942d45a50a754a2a34fdee0a5e9dab92c5281c19dc8b6c62` |

  说明：
  - `04`/`05` 头里记录的 lane 二进制 sha256 是采集时真实的被测二进制（HEAD A 时刻）；当前 lane 二进制
    已被后续构建覆盖（sha256 不同），**因此功能证据以 04/05 头里的记录值为准**，当前值只作审计。
  - `04b` 头里记录的 product 二进制 sha256 是 HEAD B 时刻真实被测二进制；当前 vedit-app 同样被后续构建覆盖。
  - `crates/vd-ffi/src/fim.rs` 的**被测版本是工作树版**（`1ca08772b0079458affbb9a39d16eabe5b0aca6839106f4b5f160f3c0f09b4f8`）；
    HEAD `d36bff9` 里的提交版 sha256 不同（clippy 修复的格式差异），两者都列出以便复核。
  - `src/editor/fim/**` 在 HEAD `d36bff9` 里**尚未提交**（untracked），被测内容以上表中的工作树 sha256 为准。
  - 所有 21 份日志（主目录 11 + `repro/` 10）在 T105 补记了 C9 头（`collected_at`/UTC/HEAD/porcelain/backfill）；
    原日志正文未改动；没有原始头的文件明确标 `collected_at: unknown`，未编造时间。

---

## 0. 一句话

`crates/vd-ffi/src/fim.rs` 从 `vd-config` 读取项目/用户 `vedit.toml` 与 `VEDIT_FIM_*` 环境变量并映射到
`vd_fim::FimSettings`；新增编辑器侧客户端 lane `src/editor/fim/`（prefix/suffix 切分 + 建议状态机 +
C ABI 适配器）。**默认关闭**时产品进程（`vedit-app --selfcheck-ffi`）与 lane 进程在
`LD_PRELOAD` syscall 计数器下均为 `SOCKETS=0 CONNECTS=0 GETADDRINFO=0`；**显式配置**后请求经
Rust 侧发出（本地假端点实测 `POST /v1/completions`、`user-agent: ureq/3.5.0`、`CONNECTS=1`）。

## 1. 交付与调用链

| 路径 | 内容 |
|---|---|
| `crates/vd-ffi/src/fim.rs` | `settings_from_config()`（`FimProtocol`/timeout/api_key 映射，key 保持脱敏类型）+ `load_settings()`（`vd_config::Loader`：项目 toml → 用户 toml → env）+ `complete_with_settings()`；C ABI 形状不变（无 header 变更） |
| `src/editor/fim/fim_backend.h/.cpp` | 接缝：`Backend::complete(prefix, suffix)`；生产实现 `makeRustBackend()` 只调用 `vd_fim_complete` / `vd_fim_poll`（**Qt 侧没有任何 HTTP 代码**）；`contextAt()` 光标切分 |
| `src/editor/fim/fim_inline_completion.h/.cpp` | 编辑器侧状态机：request / 输入作废 / accept 一次 / cancel / 短状态文案 |
| `src/editor/fim/fim_selftest.cpp` | 无头自测 40 checks（假 backend + 有 Rust 链时的本地假端点；`--default-off-only` 供 syscall 计数用） |
| `src/editor/fim/CMakeLists.txt`、`README.md` | 独立 lane 构建（`-DVEDIT_FFI_STATICLIB=… -DVEDIT_FFI_INCLUDE_DIR=…` 或 root 的 `TARGET vd_ffi`）；无 Rust 链时 selftest 显式 SKIP，不伪造 PASS |

调用链（配置后）：

```
QText/编辑动作 -> fim::InlineCompletion::request(text, line, column)
  -> fim::Backend::complete(prefix, suffix)            [Qt：只做字符串切分与状态]
  -> vd_fim_complete / vd_fim_poll                     [C ABI，crates/vd-ffi/src/fim.rs]
  -> vd_fim::FimService（enabled/endpoint/model 校验）  [crates/vd-fim]
  -> vd_fim::HttpTransport（ureq）                     [请求真正由此发出]
```

`FIM-RUST-REQUEST first_line="POST /v1/completions HTTP/1.1" user-agent: ureq/3.5.0
body_has_prompt=1 body_has_suffix=1 authorization_bearer_redacted=1`（`05-configured-chain.log`）
即为“请求从 Rust 侧发出”的链路证据；假端点（同进程 loopback）只监听，不发送。

## 2. 判据 1：默认关闭 = 零网络（可复跑）

工具：`evidence/slice3-fim/socket_probe.c`（LD_PRELOAD 计数 `socket`/`connect`/`getaddrinfo`，
退出时写出总数与每条 socket 的 domain/type）。**这是 syscall 级证据，比“代码里看起来没发请求”强**：
它也会抓到某个库在背后偷偷开 socket。

| 进程 | 配置 | 观测 |
|---|---|---|
| `target/fim-lane/vedit-fim-selftest --default-off-only` | 清空 `VEDIT_FIM_*`、隔离 HOME/XDG（无监听端口） | **SOCKETS=0 CONNECTS=0 GETADDRINFO=0**，`FIM-SELFTEST ok=1 checks=28 failures=0` |
| `target/qt6-build/src/app/vedit-app --selfcheck-ffi <fixture>` | 产品默认（无 FIM 配置） | **SOCKETS=0 CONNECTS=0 GETADDRINFO=0**，`CHECK ffi.fim.disabled-by-default PASS`，`FFI-ABI-SELFTEST ok=1 checks=20 failures=0` |
| 同一 lane 的普通自测 | 默认关闭，但**故意**起一个 loopback 监听器 | `endpoint_requests=0`（监听器一次 accept 都没有） |

日志：`04-default-off-sockets.log`、`04b-product-default-off-sockets.log`、`05-configured-chain.log`。

**这个证明覆盖什么 / 不覆盖什么（如实）**：

- 覆盖：**该进程**在默认配置下没有发起任何 `socket()`/`connect()`/`getaddrinfo()`；FIM 请求路径
  没有建 transport、没有建 worker（`vd-fim` 另有 `worker_spawns()` 计数的单元测试）。
- 不覆盖：其它进程/插件、内核层抓包（不是 pcap）、以及“编辑器 UI 触发”这条链路——**产品内还没有
  接线**（见 §5），所以产品级证据证明的是“产品链接的 FFI FIM 路径默认零网络”，不是“某个按键
  已经不会发请求”。

## 3. 判据 2：配置后经 Rust 服务可用（本地假端点，不是真实端点）

- 假端点：selftest 在 **127.0.0.1 的进程内监听**，对一次请求回固定 JSON
  `{"choices":[{"text":" assign x = 1;"}]}`。**它不是真实模型端点，真实端点可用性不作门禁。**
- 配置：`VEDIT_FIM_ENABLED=1`、`VEDIT_FIM_ENDPOINT=http://127.0.0.1:<port>`、
  `VEDIT_FIM_MODEL=local-fake-model`、`VEDIT_FIM_API_KEY=SECRET-SELFTEST-KEY`（测试用假 key）。
- 实得（`05-configured-chain.log`）：`requests=1`、`POST /v1/completions HTTP/1.1`、
  `user-agent: ureq/3.5.0`、请求体含 `"prompt"`/`"suffix"`、`Authorization: Bearer <redacted>`；
  进程 syscall 计数 `CONNECTS=1`（`SOCKETS=3` = 两个进程内假监听器 + 一个 Rust 客户端的
  `SOCK_CLOEXEC` socket；`GETADDRINFO=0`，端点是字面量 IP）。
- 半配置（`enabled + endpoint`、无 model）：Rust 侧返回「FIM 未配置模型」，**不发起新连接**
  （`endpoint_requests` 仍是 1）。
- 不可达端点：`crates/vd-ffi/src/fim.rs` 单元测试 → 显式 service error，错误文本含 `FIM`、
  **不含 key**。
- 键安全性：`Debug`/状态文本不含 key（单测与 `fim.rust.configured.no-key-in-status`）。

## 4. 门禁（自跑输出）

| 门禁 | 结果 |
|---|---|
| `cargo test -p vd-ffi fim:: -- --nocapture` | **5/5 passed**（`01-ffi-rust-tests.log`） |
| `cargo test -p vd-fim --test default_off` | **4/4 passed**（`02-vdfim-default-off-tests.log`） |
| lane ctest `vedit-fim-selftest` | **1/1 Passed**（`06-lane-ctest.log`） |
| 根 ctest | **23/23 Passed**（`06-root-ctest.log`，含 T25 settings 的新测试） |
| `bash build/gates/typecheck.sh` | **TYPECHECK-EXIT=0**（`06-typecheck.log`；含 `[qt6] PASS` 与 Rust clippy `Finished`） |
| `cargo clippy --workspace --all-targets -- -D warnings` | **exit 0**（同机另跑） |

复跑：`bash evidence/slice3-fim/run-evidence.sh`（默认写 `evidence/slice3-fim/repro/`；本次已跑，
`RUN-EVIDENCE-EXIT=0`，其中相同数字：默认关闭 `SOCKETS=0/CONNECTS=0`，配置后 `CONNECTS=1`）。

## 5. 未接线（明确声明）与所需最小改动

**编辑器侧 lane 与 Rust 调用链已验证；产品内接线待后续任务（2 个文件）。** 本 lane 没有被
`src/editor/kernel` 调用，也没有任何面板/命令注册；没有把“未接线”当成“已接线”。

要把 FIM 真正接进编辑器，最小改动是（**均不在 T27 写域**，交接线任务决定）：

1. `src/editor/CMakeLists.txt`：`if(EXISTS src/editor/fim/CMakeLists.txt) add_subdirectory(fim)`
   + `target_link_libraries(vedit_editor_core PUBLIC vedit_editor_fim)`（约 4 行，带 guard）。
2. `src/editor/kernel/ktexteditor_provider.cpp` 的 `EditCoreProvider::createEditor()`（以及
   `openFile()` 后刷新）：为每个 `KTextEditor::View` 建一个 `fim::InlineCompletion`（`makeRustBackend()`），
   连 `View::cursorPositionChanged` 与 `Document::textChanged` → `onTextEdited`；在 view 上装
   `QShortcut(QKeySequence("Alt+\\"))` → `request(document->text(), cursor.line(), cursor.column())`，
   `Tab`/`Esc` → `accept()`/`cancel()`。ghost text 渲染：KTextEditor 6.24 没有公开 ghost-text API
   （与折叠同类限制），最小实现是 view 上的半透明 `QLabel` 叠层（按 `View::cursorPosition` + 字体度量
   定位），或退化为状态栏提示——这一选择需要接线任务裁定。
   `accept()` 的文本写回文档用 `Document::insertText(cursor, text)`。

无需改 `src/app/**`（键盘触发用 view 上的 QShortcut；若想加菜单命令才需要 `src/app/main.cpp`）。

## 6. 安全边界复核

- 仓库不提供默认端点/模型/key：`crates/vd-fim/src/settings.rs` 的 `Default` 全空；本任务新增代码
  只接受配置；grep 出的 `http://` 值全是 `127.0.0.1`、测试假 key（`TEST-`/`SECRET-`）与既有单测的
  示例串，无真实凭据。
- 错误/状态文本不含请求头、key、带 query 的完整 URL（复用 `FimError::reason()` 的既有约束）。

## 7. 文件与证据

改动：`crates/vd-ffi/src/fim.rs`（配置加载 + 映射 + 5 个单测）、`src/editor/fim/**`（新 lane）、
`evidence/slice3-fim/**`。未改 `crates/vd-fim/**`（按契约复用）、`src/editor/kernel/**`、`src/app/**`、
`src/workspace/**`、`integration.cpp`、`src/app/CMakeLists.txt`。

证据文件：`01/02`（Rust 测试）、`03`（静态库 + lane 构建）、`04/04b`（零网络 syscall 证据）、
`05`（配置后 Rust 链路）、`06`（lane/root ctest、typecheck）、`socket_probe.c`、`run-evidence.sh`、
`repro/`（一键复跑输出，`RUN-EVIDENCE-EXIT=0`）。

## 8. 边界声明（F3/F4，评审要求保留）

- **F3（产品状态）**：**产品 UI 未接线** —— T27 交付的是编辑器侧 lane + Rust 服务 + 无头证据；
  没有把“未接线”说成“已接线”。把 FIM 接进编辑器仍需 §5 列出的 2 个文件的改动（`src/editor/CMakeLists.txt`
  与 `src/editor/kernel/ktexteditor_provider.cpp`），由后续任务决定/实施。截至本次补记，`git status`
  中还出现了**其它在飞任务**的未跟踪接线文件（如 `src/editor/kernel/fim_wiring.*`、`evidence/fim-wiring/`），
  它们不是 T27 的产物，也**没有**被本证据验证。
- **F4（证明工具的边界）**：`LD_PRELOAD` 是**动态插桩**，它只覆盖经由动态符号解析的
  `socket()`/`connect()`/`getaddrinfo()` 调用；**不覆盖** raw syscall 指令（直接 `syscall(...)`）、
  静态链接或 libc 内嵌的 socket 实现、以及真实网卡上的 pcap 级观测。因此“零网络”的结论范围是
  “本进程在默认配置下没有发起可被动态插桩捕获的网络调用”，不是“物理上不可能有网络包”。
