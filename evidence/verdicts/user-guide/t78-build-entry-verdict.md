# T79 独立评审结论 — T78（构建入口文档三处统正）

> **手工建任务、无冻结契约**：T78/T79 在 cell 里没有 split-key 映射/验证环，判据取自 captain 派单书；
> verdict 按要求落盘于此。评审者 ≠ writer（writer = Junior Engineer）。证据：`evidence/verdicts/user-guide/t78-build-entry-review.log`。

- **结论**：**PASS**（核心判据全部达成：新人能找到 `bash scripts/build-qt.sh` 与 `target/qt6-build/src/app/vedit-app`、
  产物路径与脚本一致、包名全部真实可核、旧产品说法与脚本一致）＋ **2 条 Low finding**（指针/术语，不影响放行）。
- 回归：`bash build/gates/docs.sh` → `[docs] PASS`，`DOCS-EXIT=0`（我实跑）。

---

## 0. 新人三问 —— 我作为“刚 clone、只读 AGENT.md + USER_GUIDE”的人的实际答案（本评审最重要产出）

1. **我会先跑哪条构建命令？它产出什么？**
   → 我会跑 **`bash scripts/build-qt.sh`**。`AGENT.md` §4 现在把它列为第一条构建项：
   「Build the **delivered Qt6 product** (v2): `bash scripts/build-qt.sh` -> `target/qt6-build/src/app/vedit-app` …
   **If you are verifying the v2 deliverable, use this command.**」——旧 `build.sh` 被明确降级为
   「legacy Rust/GTK3 product … writes `target/release/vd-app` (copied to `build/artifacts/vd-app`) and
   **never produces `vedit-app`**」。**不会再先跑 build.sh 拿 vd-app。**（T77 的缺口已闭合）
2. **我能找到 `vedit-app` 的确切产物路径吗？**
   → 能，三处一致：`AGENT.md:38`、`docs/USER_GUIDE.md` §1.4（`:62`）、`docs/BUILD.md` §12.2（`:222`）都写
   **`target/qt6-build/src/app/vedit-app`**。
3. **运行时还要装什么？**
   → 文档给出类别 + 实测包名出处：KF6/Qt6 共享库、`libtree-sitter` 0.25、Qt 平台插件（含 `libqxcb`），
   并指向 **`evidence/runtime-deps/REPORT.md` §1** 的实测 apt 清单（`docs/USER_GUIDE.md:64-66`、`AGENT.md:39-41`）；
   USER_GUIDE 还提醒构建基线 glibc 2.43、比它老的系统要连 glibc 一起打。
   **作为新人我能照着 install（有一条 `apt install` 命令在 REPORT §1.3），不会瞎猜包名。**

## 1. 事实核验（我自己读脚本/查包，不信文字）

### 1.1 产物路径与脚本一致 ✅
| 文档写的 | 脚本实际 | 结果 |
|---|---|---|
| `target/qt6-build/src/app/vedit-app`（AGENT/user guide/BUILD 三处） | `scripts/build-qt-in-container.sh:11` `BUILD_DIR="$WORK/target/qt6-build"`；`:72` `APP="$BUILD_DIR/src/app/vedit-app"`；`scripts/build-qt.sh` 委派到它 | ✅ 逐字一致 |
| 产物存在 | `target/qt6-build/src/app/vedit-app` = **71,618,608 bytes**，sha256 `49c45d1b8fafd86c…`（与 `evidence/runtime-deps/REPORT.md` 记录的 artifact 一致） | ✅ |
| 旧产品 `target/release/vd-app`（copied to `build/artifacts/vd-app`，never `vedit-app`） | `scripts/build.sh:63` `BIN="$ROOT/target/release/vd-app"`；`:112` `cp "$BIN" "$ARTIFACTS/vd-app"`；脚本全文**无** `vedit-app` | ✅ 与文档一致 |

### 1.2 包名全部真实（无编造）✅
- 文档**正文里唯一出现的 apt 包名**是 `libtree-sitter0.25` → 宿主 `apt-cache policy`：**候选 0.25.9-7**（real）。
  `libqxcb` 不是包名，是 Qt 平台插件文件 → 镜像内 `/usr/lib/x86_64-linux-gnu/qt6/plugins/platforms/libqxcb.so` **存在**（+`libqoffscreen.so`）。
- `evidence/runtime-deps/REPORT.md` §1 列出的全部包名，我用镜像内 `dpkg -S` 逐一核对，**全部由这些包实际拥有**：
  `libtree-sitter0.25`、`libkf6texteditor6`、`libkf6parts6`、`libkf6syntaxhighlighting6`、
  `libqt6widgets6`、`libqt6gui6`、`libqt6core6t64`。
  → **没有任何未经核实的包名，不触发 FAIL。**

### 1.3 旧产品说法与脚本行为一致 ✅
`build.sh` = 旧 Rust/GTK3：构建 pinned CentOS7 镜像 → `cargo build --release --workspace` → Xvfb 冒烟 → 写
`target/release/vd-app` 并复制到 `build/artifacts/vd-app`，从不产出 `vedit-app` —— 与 AGENT.md 的新描述逐条一致。

### 1.4 写域 ✅
`git status`：` M AGENT.md`、` M docs/BUILD.md`、` M docs/USER_GUIDE.md`、`?? evidence/build-entry-docs/`
（其余 modified/untracked 是并发 lane 与 gate 日志副作用，非 T78 编辑）；无代码/测试/门禁改动。

---

## 2. Findings（均 Low，非阻塞）

| ID | 严重度 | 症状 | 期望 | 证明来源 |
|---|---|---|---|---|
| **F-1** | **Low** | `docs/USER_GUIDE.md:66` 讲 glibc 打包时写「（**同文件 §2**）」——紧邻的上文引用的是 `docs/BUILD.md §12.3`，所以“同文件 §2”会指到 **BUILD.md §2「一条命令」（旧 build.sh）**，那里没有 glibc 打包内容；真正写“自包含实测/连 glibc 一起打”的是 `evidence/runtime-deps/REPORT.md` **§2** | 改成显式指针：`见 evidence/runtime-deps/REPORT.md §2（自包含实测）`，或 `docs/BUILD.md §12.6` | `docs/USER_GUIDE.md:66`；`docs/BUILD.md:20`（§2=一条命令）；`evidence/runtime-deps/REPORT.md:48`（§2=自包含实测） |
| **F-2** | **Low** | 三份文档对 `vedit-app` 的“交付物”表述不一致：AGENT.md 称 “**delivered** Qt6 product”，USER_GUIDE §1.4 称「v2 **交付物**」，而 `docs/BUILD.md §12.6` 第一条写「T10 不含运行时打包…**本节的产物是可执行的冒烟程序，不是交付物**」（打包属 T11）——新人会不确定 `vedit-app` 到底算不算最终交付物 | 统一措辞：`vedit-app` 是“v2 产品二进制（AppImage/运行时打包见 T11、BUILD.md §12.6）”；并修正 §12.6 里“冒烟程序”的过时句（§12 现在产出的是真正的 app 二进制） | `AGENT.md:38`、`docs/USER_GUIDE.md:52`、`docs/BUILD.md:296-298` |

**非缺陷提示**：`AGENT.md` 里仍有一处 `<fill in: ...>` —— 那是文件头自带的 “Replace every `<fill in: …>` placeholder …” 说明句，**不是**未填的占位值（且非 T78 引入）。

## 3. 未能验证 / 边界

1. **GUI/真机**：无显示器，未跑 `vedit-app` 窗口；“构建后能起窗口”只按脚本与已有证据（xcb/offscreen 冒烟）理解，未由我重跑。
2. **干净机器实测**：没有在“无 KF6/Qt6 的干净发行版”上实测 `apt install` 那条命令（包名已由 dpkg -S / apt-cache 证明真实，但安装组合未实跑）。
3. **CentOS 7 真机**：glibc 打包后的真实 CentOS 7 kernel 3.10 行为仍属 T12 范围（BUILD.md §12.6 已如实标注）。
4. **“新人视角”的局限**：我就是读过仓库上下文的评审者，无法完全还原“什么都不知道”的读者；上述三问答案基于我只读 AGENT.md + USER_GUIDE 时的实际理解路径。

## 4. 结论

**PASS**：T78 的三处统正达成了目标——新人会先跑 `bash scripts/build-qt.sh`、能在三份文档里找到
`target/qt6-build/src/app/vedit-app`、知道运行期要 KF6/Qt6 + `libtree-sitter` + 平台插件并知道去
`evidence/runtime-deps/REPORT.md §1` 看实测包名；产物路径与脚本逐字一致；旧产品说法与 `build.sh` 行为一致；
文档里没有编造的包名；docs gate 绿。两条 Low（F-1 指针、F-2 “交付物/冒烟程序”术语）建议随手修掉，不影响本次放行。
