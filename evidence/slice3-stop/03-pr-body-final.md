# VEdit v2 — 最终交付（停点 2）

> **状态：等用户在有屏机器上亲手验证。未合并、未打 tag、未发 Release。**
> 本 PR 是整个 v2 迁移的交付出口；`dev` 是集成分支，`main` 只在发布时移动。

---

## 1. 交付物

| 项 | 值 |
|---|---|
| 文件 | `VEdit-0.1.1-x86_64.AppImage` |
| 大小 | **95,349,240 bytes**（约 91 MiB） |
| **sha256** | **`e7ae067fe66c8e271ba72da974795a7ee3db0efa59bda45079b5a048b4a09558`** |
| built from | **`55a874da076d605c6b620e0bb20f9ac5932d5f46`** |
| 位置 | **本服务器**：`packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage`（该目录被 gitignore，**不在 git 里**） |
| PR | https://github.com/HaroldZ32/VEdit/pull/11 （base `dev`） |

## 2. 怎么拿到它（95 MB 二进制不在 git 里）

```bash
# 1) 先在本服务器上核对 sha256 —— 确认产物本身没坏
sha256sum packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage
# 期望：e7ae067fe66c8e271ba72da974795a7ee3db0efa59bda45079b5a048b4a09558

# 2) 拷到你的 CentOS 7 VM（把 <user> <host> 换成你的）
scp packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage <user>@<host>:/tmp/

# 3) 到了 VM 上再核一次（防传输损坏）
sha256sum /tmp/VEdit-0.1.1-x86_64.AppImage
# 必须与上面完全一致，再执行

# 4) 运行（CentOS 7 常无 fusermount3，用解压模式）
chmod +x /tmp/VEdit-0.1.1-x86_64.AppImage
/tmp/VEdit-0.1.1-x86_64.AppImage --appimage-extract-and-run
```

## 3. 已验（无头 / 容器内，可复跑）

| 项 | 结果 |
|---|---|
| 四宿主容器矩阵 | **4/4 PASS**：`centos:7`（glibc **2.17**）/ `ubuntu:24.04` / `rockylinux:9` / `ubuntu:26.04`，均**未装**任何 Qt/KF6/verible/slang |
| 应用自检 | `APP-SHELL-SELFTEST ok=1 checks=200 failures=0` |
| 随包工具链 | `TOOLCHAIN source=verible:bundled slang:bundled missing=[]` |
| 语法定义 | `definitions=3`、`own-xml=[vedit-systemverilog.xml,vedit-verilog.xml]`、`verilog=1` |
| 折叠 | `fold.wiring.closed.hidden-975 PASS` |
| 自带 loader/libc | `/proc/maps` 证明加载的是包内 `ld-linux` + `libc` |
| 交付边界 | `verify-not-shipped.sh --expect-clean` → **pass=8 fail=0**，`DELIVERY_BLOCKED=no` |
| F2 隔离反向验证 | **5/0**，反向探测 **403**（未授权访问被拒） |
| FFI 静态链接 | `libvd_ffi` NEEDED = **0**；导出 `T vd_*` = **44** |
| 干净树 ctest | **27/27**（`100% tests passed, 0 tests failed out of 27`） |
| 干净 worktree typecheck | `CLEAN-TYPECHECK-EXIT=0` |

证据索引：`evidence/slice3-pack/`（构建矩阵、AppDir 清单、`SHA256SUMS`、`RUNTIME-NOTICES.txt`、四容器日志）、`evidence/slice3-stop/`（本停点）、`evidence/runtime-deps/`（运行期依赖实测与自包含验证）。

## 4. 未验（必须如实列，且不假装验过）

| 项 | 状态 |
|---|---|
| **真 CentOS 7（内核 3.10）** | ❌ **待用户实机验证**。容器共用宿主内核，测不到 3.10 |
| **GUI 双击 / FUSE 挂载 / 真实交互** | ❌ 未验。执行服务器**无显示服务器**，任何"截图看起来对"都不算 |
| **SVN 真子进程路径** | ❌ **stub-only**：宿主与打包镜像都没有 `svn`/`svnadmin`，只有 Git 是端到端验证过的 |
| **A4.5 视图级折叠观感** | ❌ 未验（只有无头断言） |
| **FIM ghost 多行** | ⚠️ 已知限制：只预览首行 |
| **appimagetool 网络下载路径** | 已改用仓内 vendor（sha256 `793fca0c…`，fail-closed）；网络下载路径不再使用 |

## 5. 已知设计边界（不是缺陷，是约定）

- **VCS 面板**：三个读操作全部是异步 job（`vd_vcs_status` / `vd_vcs_info_job` / `vd_vcs_diff_job` + poll），**GUI 线程永不跑 `git`/`svn` 子进程**；无写入口。Git 端到端验证，SVN stub-only。
- **设置**：以 Rust settings store 为权威；UI 冲突有提示、坏配置回退。
- **FIM**：**默认关闭**；未配置时零网络（syscall 级证明 `SOCKETS=0 CONNECTS=0 GETADDRINFO=0`）。
- **工具链发现**：`bundled > local > PATH > env`，缺工具时只显示**一条**状态句，不刷屏。

## 6. 构建方式（从源码复现）

```bash
bash scripts/build-qt.sh            # -> target/qt6-build/src/app/vedit-app
bash packaging/runtime/build.sh     # -> packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage
```

- 工具链版本 pin 在 `scripts/versions.env` / `scripts/versions-qt.env`。
- `appimagetool` 使用**仓内 vendor**（`packaging/appimage/vendor/appimagetool-x86_64`，sha256 `793fca0ce4bcc309dc65d3f66669f58e3755643e4dadbe03932e7b436128494f`，与 pin 逐字节一致），上游漂移时 fail-closed。
- **复现边界**：产物 `e7ae067f…` 绑定 `55a874d` + 当前打包脚本；任何后续源码/脚本改动都会让它失效。

## 7. 评审 verdict（交付相关五份）

| verdict | 结论 |
|---|---|
| [`evidence/verdicts/pack/verdict.md`](evidence/verdicts/pack/verdict.md) — T32 最终打包 | **PASS**（AC-S3.3 / AC-S0.4 / glibc ≤2.17），附 N1–N5 非阻塞 finding |
| [`evidence/verdicts/appdir-toolchain/verdict.md`](evidence/verdicts/appdir-toolchain/verdict.md) — T87（T84 `VEDIT_SYNTAX_DIR` + T85 工具链 UX） | **PASS** |
| [`evidence/verdicts/ctrl-s-highlight/t88-verdict.md`](evidence/verdicts/ctrl-s-highlight/t88-verdict.md) — T88（T80 Ctrl+S 单一权威 + T81 真 KTextEditor 高亮） | **PASS** + 2 条 Low |
| [`evidence/verdicts/supporting-deliverables/verdict.md`](evidence/verdicts/supporting-deliverables/verdict.md) — T89（T83 真机验证包 + T82 随包工具链 + T86 gitignore） | **PASS**（带 findings） |
| [`evidence/verdicts/fim-reeval/verdict.md`](evidence/verdicts/fim-reeval/verdict.md) — T107 FIM 证据复审 | **PASS**（T27 证据可放行） |

其余 lane 的 verdict 全量索引见 `evidence/verdicts/**`。

## 8. 用户验收要做的事（AC-S3.4）

1. `sha256sum` 核过再运行（见 §2）；
2. 双击 / 运行 AppImage → 看窗口起来；
3. **打开 3 个文件** → **改一行** → **Ctrl+S** → **看诊断出现**；
4. 顺手看：**语法高亮**、**lint 诊断**、**命令面板**、**VCS 面板**。

> ⚠️ **真 CentOS 7（内核 3.10）未经验证** —— 本次就是请你在真机上验这一步。

## 9. 待补（本 PR 定稿前）

- **T112**：修 T32 提出的三条证据问题（N1 `--no-test` 日志被当 ctest 证据 / N2 52 份证据缺 C9 头 / N3 `out/AppDir` 旧 sha）。
- **T113**：真机验证包（`verify-appimage-centos7.sh` + 中文 STEPS），落地后路径与用法补进本节。

## 10. 红线

**未合并、未打 tag、未发 Release。** 交付出口 = 本 PR + 该 AppImage。
