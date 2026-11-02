# smoke 二进制的 sha256 为什么有三份（给 T11 / T12 / T43 / T62 的身份说明）

一句话：**`vedit-m0-smoke` 的字节不是稳定身份**。它在 T10 之后被合法地扩展过（T60 加了自建
KSyntaxHighlighting 探针参数），而且 C++ 二进制（RelWithDebInfo）会随构建目录/调试信息变化——
同一份源码在不同构建树里重建，sha256 也会不同。判断"是不是同一份东西"要看去重三元组
（源码 sha256 + 构建参数 + 镜像），**不要**把二进制 sha256 当版本号。

## 三份 sha256 的来源

| sha256 | 出现在 | 源码年代 | 构建树 |
|---|---|---|---|
| `f0f53bfd…` | T10 证据（`evidence/m0-build/`，`34-qt6-elf.txt` 原件、`REPORT.md`） | T10 版 `qt6_window_smoke.cpp`（无 `--ksh-selfbuild` 等 T60 参数） | T10 的 `target/qt6-build` |
| `d629a1ad…` | T11 `evidence/m0-runtime/REPORT.md`（其 AppDir 打包的那份） | **T60 扩展版**（`cmake/smoke/` 在 11:31–11:40 被 T60 修改，出现 `whitelist_verilog=` 等字段） | T11 自己的构建树 |
| `cc9f6778…` | 当前树重建（`target/qt6-build/cmake/smoke/vedit-m0-smoke`） | T60 扩展版 + T61 新增独立目标 `vedit-ksh-env-probe`（**不改** smoke 源） | T60/T61 脚本的 `target/qt6-build` |

`d629a1ad…` 与 `cc9f6778…` 的源码相同（T60 版 smoke），差异来自构建树状态/路径（DWARF 调试信息
会编码构建目录），这正是"二进制 sha256 不能跨构建树比较"的证据。

**注意**：`evidence/m0-build/build-logs/34-qt6-elf.txt` 现在显示的是 `cc9f6778…`，因为它是 T62
事故后**重跑**的 11 份 `build-logs/` 之一（不是 T10 原件）。T10 对 `f0f53bfd…` 的引用见
`evidence/m0-build/REPORT.md` 与评审记录。

## 当前树的身份（可复跑）

```sh
sha256sum cmake/smoke/qt6_window_smoke.cpp cmake/smoke/CMakeLists.txt cmake/smoke/ksh_env_probe.cpp
# 15e8e2e0…  qt6_window_smoke.cpp        （T60 扩展：--ksh-selfbuild/--expect-empty-syntax/
#                                         --custom-syntax-dir/--highlight-check/--expect-kshlib-prefix）
# 0c9115e4…  cmake/smoke/CMakeLists.txt  （T61 追加 vedit-ksh-env-probe 目标）
# 65c66cb0…  cmake/smoke/ksh_env_probe.cpp（T61 诊断程序）
bash scripts/build-qt.sh --no-test        # 或 scripts/build-ksh-selfbuild.sh（也构建 smoke）
sha256sum target/qt6-build/cmake/smoke/vedit-m0-smoke   # cc9f6778…
```

## 对 A0c 的影响：无

A0c 的判据是**行为**：`M0-SMOKE … libc=<path>` 且 `--expect-libc-prefix <AppDir>/usr/lib`
断言通过（进程用包内 loader/glibc）。它与 smoke 的版本字段、参数集合、二进制 sha256 无关。
T60/T61 新增的字段（`whitelist_verilog=`、`kshlib=`、`builtin_valid_defs=`）是**同一行上的追加
key=value**，A0c 只读 `libc=`。因此：

- T11 已打的 `d629a1ad…` 那份**可以继续用**，A0c 语义不变；
- 将来重建会得到不同 sha256（构建树不同），但 A0c 仍成立；
- T12/T43 复跑请**解析 key=value**（`ok=1`、`libc=`、`window=1` 等），不要整行字符串比对，
  也不要用二进制 sha256 判定"同一份产物"。

推荐以后归档时记录三元组而不是单个哈希：
`源码 sha256 清单 + 构建命令/参数 + 镜像 id/digest + 二进制 sha256（参考值）`。
