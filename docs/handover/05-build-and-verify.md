# 05 · 构建、验证与取证

> 这一份只回答两件事：**怎么把它建起来**，以及**怎么证明它真的建对了**。
> 第二件事比第一件重要 —— 这个项目已经三次被"绿但没编译"骗过。

## 1. 命令清单

| 目的 | 命令 | 备注 |
| --- | --- | --- |
| 构建 Qt6 产品 | `bash scripts/build-qt.sh` | 进固定镜像 → cmake/ninja → ctest。**当前 exit 0，ctest 9/9** |
| 只配置不改文件 | 见 `scripts/build-qt-in-container.sh` | 内部用，一般不用手调 |
| 旧 Rust 产品构建 | `bash scripts/build.sh` | 与 Qt6 无关；**不要**用它验证新产品 |
| 旧 Rust 产品门禁 | `bash scripts/gates.sh` | cargo/clippy/fmt —— **只对旧产品有效**（见 §4） |
| 门禁包装 | `bash build/gates/gates.sh` 等 | `gates.sh` / `tests.sh` / `typecheck.sh` / `manifest.sh` / `docs.sh` 各管一块 |
| 语义高亮探针 | `bash build/slang/build.sh` | SLANG 深检二进制 |
| 运行时自检 | `bash packaging/runtime/selfcheck.sh --appimage "$PWD/packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage"` | 断言 M0-SMOKE、`libc` 包内解析、KSH 白名单 |
| 四宿主矩阵 | `bash tests/packaging/hostmatrix.sh --appimage <**绝对路径**>` | **相对路径会失败** |
| 组 AppDir | `bash packaging/runtime/assemble-appdir.sh` | 产出 `packaging/runtime/out/AppDir`（已 .gitignore） |
| 出 AppImage | `bash packaging/runtime/build-appimage.sh` | `appimagetool --no-appstream` |
| 验证不该带的东西没带 | `bash packaging/runtime/verify-not-shipped.sh` | 必需 `--expect-clean` 断言 |
| F2 隔离验证 | `bash packaging/runtime/verify-f2-isolation.sh` | 宿主定义隔离 |
| 编辑内核算术探针 | 见 `src/editor/probes/README.md` | 一条命令重跑三个探针，已实测 exit 0 |

## 2. 取证规则（三条，都是事故换来的）

### 2.1 「构建通过」必须附带 **target 清单 + 一次证伪**

只贴 `exit 0` 不算数。必须同时给出：

1. 实际编译到的 target 名与磁盘产物（例：`libvedit_terminal.a` 7.8 MB、`vedit-app` 18.9 MB）
2. 链接证据（`ninja -t commands` / `ninja -t query` / `nm -C` / `readelf -d`），**不许只贴 CMakeLists 文本**
3. **证伪**：往该 lane 里放一行 `#error`，构建必须变红；再移除

> 由来：根 `CMakeLists.txt` 曾经没列 lane 目录，`src/syntax/provider` **编译了 0 个文件**而门禁全绿。
> 参考实现：`evidence/build-registration/00-counterexample.txt`（红）+ `01-build.txt`（绿）+ `02-targets.txt` + `03-link.txt`。

### 2.2 证据必须带**采集时间 + 树状态**

每条证据文件头部写清：采集时间、`git rev-parse HEAD`、`git status --porcelain` 是否干净。
每个里程碑结束，在**冻结的树**上重采一次。

> 由来：有两次拿"红着结束"的日志当编译证据用（`cmake/smoke/34-qt6-elf.txt`、`32-qt6-build.log`），
> 还有一次证据文件比代码老（`evidence/app-shell/08-structure.txt` 行数 398 vs 实际 400）。

### 2.3 自测 ≠ 评审

自测日志只能证明"作者认为它对"。**独立评审必须由另一个 agent 做，且优先读契约与证据、不读实现**。
上一轮 5 条 lane 只有自测没有 verdict，在状态矩阵里一律记为未完成。

## 3. 构建链的真实形状

```
CMakeLists.txt (根)
├── VEDIT_LANE_DIRECTORIES   ← 显式清单，NO GLOB
│     src/syntax/provider, src/syntax/kde, src/syntax/grammars,
│     src/terminal, src/syntax/overlay(尚不存在 → if(EXISTS) 跳过)
├── if(EXISTS src/app/CMakeLists.txt) → add_subdirectory(src/app)
│     └── src/app/CMakeLists.txt 里再 if(EXISTS) 收录 src/workspace/*
└── 每个 lane 目录 各自 CMakeLists（静态库 + 可选 ctest）
```

**为什么用显式清单而不是 GLOB**：GLOB 在文件新增时不会自动生效，而且目录写错时静默为空 ——
这正是"编译 0 个文件却全绿"的成因。改为显式清单 + `if(EXISTS)` + `message(STATUS ...)` 之后，
配置期日志里能直接看到"哪些 lane 被编了、哪些被跳过"。

**当前配置期的真实输出**：

```
building lane directory src/syntax/provider
building lane directory src/syntax/kde
building lane directory src/syntax/grammars
building lane directory src/terminal
src/syntax/overlay not present yet; skipped
```

## 4. 门禁的真实语义（**这段最容易被误读**）

`build/gates/*.sh` 与 `scripts/gates.sh` **仍然是给旧 Rust/GTK 产品用的**：

| 门禁 | 实际在测什么 | 对 Qt6 产品的意义 |
| --- | --- | --- |
| `gates.sh:43-52` | 硬要求 `cargo` | Qt6 产品**完全不覆盖** |
| `vendor.sh:15` | 只查 `slang/verible/gtk` | 不查 Qt6 依赖 |
| `dist.sh:7` / `pack.sh:8` | 找旧产物 | 与 AppImage 无关 |
| `tests.sh:4-7` | ledger 不可达 | 实际不跑测试 |
| `comments.sh` / `rows.sh` | 永远 RED | 别当阻塞 |

**结论：看到"gates 全绿"不能读成"Qt 产品完成"。** 这一条是 `03-architecture.md` 的 G10。
下一轮应当给 Qt6 产品建一套自己的门禁，而不是继续复用旧产品的。

## 5. 打包链的三个坑

1. **`assemble-appdir.sh` 现在装的是 M0 冒烟程序**，装成 `bin/vedit-m0-smoke`；而 `AppRun:67` 找的是
   `bin/vd-app`。也就是说 **AppImage 里从来没有装过 `vedit-app`**（G7）。
   要换产品，改 `assemble-appdir.sh:62` 的输入为 `target/qt6-build/src/app/vedit-app`。
2. **`verify-not-shipped.sh` 必须有 `--expect-clean`**，否则自建 KSyntaxHighlighting 被发行版同名库
   顶替时会静默回退到 GPL 定义，而且看上去一切正常（这个真的发生过）。
3. **宿主矩阵传绝对路径**，相对路径会失败；且矩阵跑的是"产物 + 四个宿主"，产物换了要重跑。

## 6. 一条命令验收当前状态（建议每个里程碑都跑）

```bash
bash scripts/build-qt.sh \
  && bash packaging/runtime/selfcheck.sh --appimage "$PWD/packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage" \
  && grep -c PASS evidence/build-registration/01-build.txt
```

三者都要绿：**构建绿 + 运行时自检绿 + 证据里能数出 PASS**。
