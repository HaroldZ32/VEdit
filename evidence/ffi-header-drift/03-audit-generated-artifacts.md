# T123 — 「生成物被入库且被构建消费」审计

## 结论

**`crates/vd-ffi/include/vd_ffi.h` 是唯一一个“生成物 + 入库 + 被构建消费”的文件。**

| 检查项 | 结果 | 依据 |
|---|---|---|
| `crates/vd-ffi/include/vd_ffi.h` | **生成物**（`crates/vd-ffi/build.rs` → cbindgen，每次 cargo build 重写），**被跟踪**，被 C++/Qt6 构建 `#include` | `build.rs:16-40`；`vcs_source.cpp`/`settings_client.cpp` 等 `#include "vd_ffi.h"` |
| `.gitignore` 是否忽略它 | 否（tracked；`git check-ignore` 无命中） | `.gitignore` 只忽略 `target/`、`build/artifacts/` 等输出 |
| 其它 cbindgen 产物 | 无（其它 `crates/*/include/` 不存在） | `git ls-files 'crates/*/include/*'` |
| 版本戳头 | 无生成物；`cmake/VEditVersions.cmake` 直接读取**手写、已入库**的 `scripts/versions-qt.env`，并对缺失/重复 pin 直接 FATAL | `cmake/VEditVersions.cmake:13-28` |
| manifest / 资源清单 | `packaging/**` 的清单是手写输入，由打包脚本消费；不是构建输出 | `packaging/runtime/**` |
| `resources/syntax/*.xml` | 手写输入（T29 的 fold XML） | 无生成标记 |
| `evidence/**` | 不被构建消费 | 构建图里无引用 |
| `**/CMakeFiles/`、`**/cmake_install.cmake`、`build/**` | 已忽略的输出，不入库 | `.gitignore` |

## 防漂移检查（本次新增，`build/gates/typecheck.sh`）

```bash
check_ffi_header_drift()   # 在 Rust 阶段（cargo 已用 build.rs 重新生成头文件）之后运行
  git cat-file -e HEAD:crates/vd-ffi/include/vd_ffi.h   # 必须被跟踪，否则 FAIL
  git show HEAD:<header> > tmp
  diff -u tmp <header>                                   # 不一致 → FAIL（不是 warn）
```

- 不降级：任何差异 `exit 1`，与 typecheck 其它失败同等级。
- 兼顾开发：工作区里头文件被重新生成是正常的（每次 cargo 构建都会），但**入库版本必须与生成结果一致** —— 不一致说明“提交的代码不能编过”，正是 T123 的缺陷。
