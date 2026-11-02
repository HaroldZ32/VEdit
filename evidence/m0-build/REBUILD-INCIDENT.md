# 证据丢失事故补件（T62）— `evidence/m0-build/` 重建说明

- 事故：2026-10-31，另一个 lane 为跑门禁把 `evidence/m0-build/` 备份到 `/tmp`，而本会话的 `/tmp`
  是**逐命令隔离**的，备份在下一条命令即消失；随后 `rm -rf` + 解包失败，目录不可恢复（未提交，
  git 无法找回）。
- 本目录现在的内容 = **事故后重建/重跑**的产物 + 少量幸运幸存的文件。**原件已不可得**。
- 重建脚本：`evidence/m0-build/rebuild-incident.sh`（可复跑；每个输出文件头部写明
  `rebuild after evidence-loss incident, <时间戳>` + 生成命令 + 当时的镜像 id）。

## 1. 已重建（内容可重建，但**不是**原件）

| 文件 | 重建方式（脚本内对应段） | 与事故前原件的差异 |
|---|---|---|
| `01-image-facts.txt` | 在固定镜像内 `head /etc/os-release` / `ldd --version` / 工具链 / 关键包 `dpkg-query` / `pkg-config` / `/opt/vedit/build-image.txt` | 内容为同一镜像的当前事实；**镜像 id 会随重建变化**（头部记录当次 id），不要把 id 当常量 |
| `02-glibc-loader-probe-raw.txt` | loader 身份 + `strings \| grep "kernel too old"`（无匹配）+ `libc.so.6` 的 `Minimum supported kernel: 3.2.0` + ABI note | 与原件同结论；原件的 `readelf -S` 段落顺序一致，时间戳不同 |
| `03-elf-isa-notes.txt` | `readelf -n` libQt6Core/libc/libKF6TextEditor + KTextEditor 的 NEEDED | 同结论（`x86 ISA needed: x86-64-baseline`、ABI tag 3.2.0） |
| `04-abi-tag-probe.txt` | libc/ls/crt 的 ABI tag + kernel 相关字符串 | 同结论 |
| `05-runtime-data-probe.txt` | QPA 插件目录、`/usr/share/kf6`、KSH 外部数据（无）、katepart 插件、`tree-sitter.pc` | 同结论 |
| `06-data-packages.txt` | KSH/KTE 包 + katepart 插件 + 库文件 + 数据包文件表 | 同结论 |
| `06b-syntax-data-layout.txt` | KSH 数据包文件表（只有翻译）+ 定义实际所在（`libKF6SyntaxHighlighting.so.6` 内嵌） | 同结论 |
| `07-kate-data-probe.txt` | `apt-get download kate-data` + `dpkg -c` 列表 | 归档当前版本 `4:25.12.3-0ubuntu1`（与事故前一致），sha256 见文件内 |
| `08-locked-package-versions.txt` | 对 `scripts/docker/qt6-locked-packages.txt` 的每个包 `dpkg-query` 并断言版本相等 | **自校 PASS**（见 §3）；镜像 id 不同 |
| `09-locked-deb-sha256.txt` | 对锁定包逐个 `apt-get download` + `sha256sum` + 与锁文件比对 | 原件是 **20** 个包；T60 的锁定矩阵已扩到 **22**（新增 `qt6-declarative-dev`/`qt6-speech-dev`），重建文件如实记录 22 个，全部 sha256 匹配 |
| `10-kf6-cmake-requirements.txt` | `KF6TextEditorConfig.cmake` + version 文件 | 同结论；上游 `REQUIRED_QT_VERSION` 证据在 `upstream/` |
| `11-scratch-cmake-probe.txt` | **重放**：`find_package(KF6 ...)` 总包 → configure 报错 | 原件是探针容器里的交互输出；重放给出同一失败（容器/时间戳不同） |
| `11b-scratch-cmake-probe.txt` | **重放**：逐框架 `find_package` + 缺 `Definition` include → 编译错误 | 同一错误类别（incomplete type）；具体行号随容器不同 |
| `11c-scratch-cmake-probe.txt` | **重放**：补 include → `BUILD OK` / `RUN OK` | 同结论。**每个探针在独立容器运行**，故 11c 自带 scratch 项目（脚本已修正） |
| `12-ksyntax-definitions-probe.txt` | 在镜像内构建并运行定义探针 | 同结论：451 个定义、Verilog/SystemVerilog 按名字可用、`test.v` → `"V"`(vlang)、30 主题 |
| `13-bundle-probe-build.txt` | 用当前 smoke 二进制组装 CentOS 7 探针 bundle（loader+glibc+插件+148 个 NEEDED 库） | 用**当前树**的 smoke（sha256 `cc9f6778…`）而非事故前那份；bundle 大小 168M |
| `14-centos7-loader-probe.txt` | 在 `centos:7` 里用包内 loader `--list` | **修正版**：事故前的 `14` 是一次"相对路径写错"的失败尝试（正确版原在 `14b`）；重建把"正确的 loader 探针"写入 `14`、应用运行写入 `14b`，两者都保留 |
| `14b-centos7-smoke-bundle-probe.txt` | 在 `centos:7`（glibc 2.17）里经包内 loader 运行 smoke，断言 libc 前缀 | 同结论：`M0-SMOKE ok=1`、`libc=/probe/bundle/lib/libc.so.6` |
| `build-logs/40-verify-gates.log` | 重跑 `bun run verify:gates` | **不是**事故前那次运行的输出，且**这次实际 exit=1**：不是 T62 或证据重建造成的，而是当前树里**其他 lane 的在飞代码**（`src/workspace/search/search_integration.cpp` 的 `createSearchPanel` 未定义；`src/workspace/palette/palette_selftest.cpp:9` 的续行注释触发 `-Werror=comment`）导致 C++ lane 编译失败。T62 边界不允许改代码，故如实记录；同一日志同时证明 T63 的 lane 覆盖已生效（`src/syntax/provider`、`src/syntax/kde` 都在编译，provider 的库/测试已链接成功） |
| `upstream/` | 从**同一批 pinned commit** 重新抓取 5 个 KDE `CMakeLists.txt`（`ktexteditor/kconfig/syntax-highlighting v6.24.0`、`kconfig/kcoreaddons v6.0.0`） | 内容确定（raw GitHub 同 commit），附 `REBUILT-README.md` 说明；`kconfig v6.0.0` 仍为 `REQUIRED_QT_VERSION 6.5.0` |

## 2. 不可重建（如实列出，不编内容）

1. **事故前的原始字节**：没有任何副本（未提交 + `/tmp` 备份逐命令隔离），git 也没有。现在所有文件
   都是重建/重跑版本，头部均有标注。
2. **`11`/`11b`/`11c` 的原始交互顺序与容器 id**：重放给出同样的步骤和同样的失败/成功结论，但
   原始输出中的容器名、时间戳、行号无法恢复。
3. **事故前 `40-verify-gates.log` 的那次运行**：当时树里还没有 T60/T61/T63/T16/T22 的 lane；重跑只能给出
   **当前树**的结果，而当前树里其他 lane 的在飞代码让这次重跑 exit=1（原因见 §1 该行）。那次运行的
   完整原文不可重建；门禁变红也不是补件造成的，需由对应 lane 修好代码后重跑（T62 不改代码）。
4. **事故前 `14` 的失败原文**：它记录的是"相对路径写错"的一次失败探针；重建版记录修正后的探针
   （见上表），失败结论仍可从 `REPORT.md`/本文件追溯，但那次的原始字节不可得。

> 结论：**没有任何一份重建文件被伪装成原件**；每份头部都写明重建时间、生成命令与镜像 id。

## 3. `08` / `09` 自校结果（captain 指定）

`08-locked-package-versions.txt` 末段（原文）：

```
SELF-CHECK: PASS — every installed version equals the lock matrix (scripts/docker/qt6-locked-packages.txt)
```

`09-locked-deb-sha256.txt` 末段（原文）：

```
## lock matrix has 22 packages
SELF-CHECK: PASS — every downloaded .deb sha256 equals the lock (scripts/docker/qt6-locked-packages.txt)
```

与 `scripts/versions-qt.env` 的交叉核对（`08` 中对应行）：

| `versions-qt.env` | `08` 实测 |
|---|---|
| `VEDIT_QT6_VERSION="6.10.2"` | `qt6-base-dev 6.10.2+dfsg-7` ✅ |
| `VEDIT_KF6_VERSION="6.24.0"` / `VEDIT_KTEXTEDITOR_VERSION` / `VEDIT_KSYNTAXHIGHLIGHTING_VERSION` | `libkf6texteditor-dev 6.24.0-0ubuntu1` 等 ✅ |
| `VEDIT_TREE_SITTER_VERSION="0.25.9"` | `libtree-sitter-dev 0.25.9-7`（pkg-config 0.25.9）✅ |
| `VEDIT_QT6_DISTRO_GLIBC="2.43-2ubuntu2.4"` | `libc6 2.43-2ubuntu2.4` ✅ |

## 4. smoke 二进制差异（captain 同时问的）

**是 T60 的改动**（不是 T61）：`cmake/smoke/qt6_window_smoke.cpp` 在 11:31–11:40 被 T60 加了
`--ksh-selfbuild/--expect-empty-syntax/--custom-syntax-dir/--highlight-check/--expect-kshlib-prefix`，
`whitelist_verilog=` 字段由此而来；T61 只新增独立的 `cmake/smoke/ksh_env_probe.cpp` + 一个独立
target，**没有改 smoke 源**。`f0f53bfd…`（T10 源）与 `d629a1ad…`（T60 源、T11 构建树）对不上是
**源码年代 + 构建树**两重差异；当前树重建为 `cc9f6778…`（见本次 `13-bundle-probe-build.txt`）。
**不要重新归档二进制当身份**：判断同一份产物用「源码 sha256 + 构建参数 + 镜像 id」三元组；
A0c 只依赖行为（`libc=` 前缀断言），不受影响。详见
`evidence/m0-ksh-selfbuild/SMOKE-BINARY-IDENTITY.md`。

## 5. 复跑

```sh
bash evidence/m0-build/rebuild-incident.sh     # 重建 01..14b（约 4–6 分钟，需要网络 + 固定镜像）
bun run verify:gates                           # 重建 build-logs/40-verify-gates.log 的内容
python3 evidence/m0-build/verify-lock-matrix.py  # （可选）独立复核 08/09 与锁文件一致
```

独立复核（不依赖文件内自检）：`python3 evidence/m0-build/verify-lock-matrix.py` 实测输出：

```
OK: 08 lists all 22 locked packages with matching installed versions
OK: 09 verifies all 22 locked .deb sha256 values
OK: VEDIT_QT6_VERSION=6.10.2 matches 08 (6.10.2)
OK: VEDIT_KF6_VERSION=6.24.0 matches 08 (6.24.0)
OK: VEDIT_TREE_SITTER_VERSION=0.25.9 matches 08 (0.25.9)
OK: VEDIT_QT6_DISTRO_GLIBC=2.43-2ubuntu2.4 matches 08 (2.43-2ubuntu2.4)
RESULT: PASS — rebuilt 08/09 are consistent with the current lock matrix
```
