# 给 T11（私有运行时收包）的说明 — 自建 KSyntaxHighlighting（B′，T60/T61）

> **T61 已定案：注入形态 = F2**（`NO_STANDARD_PATHS=OFF` + AppRun 设置 `XDG_DATA_DIRS` 与
> `XDG_DATA_HOME`，不追加宿主路径、不留 `syntax-bundled/`，并加运行时白名单守卫）。
> 决定与实测见同目录 **`FORM-DECISION.md`（C1–C8 条件）**；本文 §3 的 A/B 讨论是 T60 当时的历史，
> 最终接线以 `FORM-DECISION.md` 为准。选定形态的构建日志在 `build-logs/`（F2），F1 对照在
> `build-logs-form-f1/`；自建库路径为 `target/ksh-selfbuild/{F1,F2}/install/lib/x86_64-linux-gnu/`。

本文件是 T60 的交付说明：**哪些文件必须随包、哪些不得随包、运行期怎么接线、怎么复验**。
构建/验收细节见同目录 `REPORT.md`；原始日志在 `build-logs/`。

## 1. 必须随包（B′ 的运行时形态）

| 文件 | 来源 / 说明 |
|---|---|
| `libKF6SyntaxHighlighting.so.6.24.0` + `libKF6SyntaxHighlighting.so.6` / `.so` 符号链接 | **自建版本**：`bash scripts/build-ksh-selfbuild.sh` 产出，位于 `target/ksh-selfbuild/F2/install/lib/x86_64-linux-gnu/`（F1 对照：`.../F1/install/...`）。SONAME `libKF6SyntaxHighlighting.so.6`；无 RUNPATH；大小 ≈696 KB；本次 sha256 `2d19bd15849fa4f98601bb55f9ea3eca8a1c35d10058d37ae2ca71312ef750ce`（两次独立全量构建相同，见 `REPORT.md` §2）。**不要**用发行版同名库替换它 |
| KTextEditor 6.24.0（`libKF6TextEditor.so.6`）+ `katepart.so` 插件 | 发行版文件；ABI 已实测兼容（`build-logs/31-symbol-diff.txt`、`32-abi-loading.txt`） |
| Qt 6.10.2 运行库（`libQt6Core/Gui/Widgets/Network/PrintSupport/Qml/TextToSpeech` 等） | 与 M0 锁定一致（T11 按其依赖闭包收） |
| `libtree-sitter.so.0.25` | M0 运行时清单（`scripts/versions-qt.env`），未变 |
| 包内 loader + glibc（M0 私有运行时） | 既有方案；A0c 自检钩子见 `evidence/m0-build/14b-*` |
| **白名单语法定义**（纯 XML，放 `<AppDir>/share/.../syntax/`） | 随包定义由 T16/ADR §5 决定；T60 只证明机制。白名单文件夹具是上游 `data/syntax/verilog.xml`（`license=""`，**仅测试用**，不要当交付物直接发） |
| 主题 | 30 个 `.theme` 仍内嵌在自建库里（上游无开关），且全部声明 MIT；无需另收文件 |

## 2. 不得随包（否则 B′ 失效）

| 不得随包 | 原因 / 检测 |
|---|---|
| 发行版 `libKF6SyntaxHighlighting.so.6.24.0`（2,010,872 B） | 内嵌 450 定义 / 36 个严格 GPL（含 SystemVerilog GPLv3+）。检测：`python3 evidence/r2-ksh-embedded/scan-embedded-defs.py --expect-clean <pkg-lib>` 必须 exit 0；发行版库会 exit 1 |
| 自建安装树里的 `share/org.kde.syntax-highlighting/syntax-bundled/`（450 个 XML + index） | 那是**构建产物**（QRC_SYNTAX=OFF 时安装为文件），不是白名单；只允许拷贝白名单条目（及其 `include` 依赖，如 `comments.xml`） |
| `systemverilog.xml`（以及其它 GPL 系定义） | C7/Open Q1 未决；白名单未包含它们，任何随包都必须有 captain/T16 的明确决定 |
| `src/`、headers、CMake config、`bin/ksyntaxhighlighter6` | 构建产物，不是运行时需要 |

## 3. 运行期接线（两种形态，二选一 —— 决策权在 captain/T16/T11）

T60 按指令构建并验证的是 **A：`NO_STANDARD_PATHS=ON`**；实测发现它对 KTextEditor 有一个钩子问题：

- **A. `QRC_SYNTAX=OFF` + `NO_STANDARD_PATHS=ON`（本次交付）**
  - 定义只能通过 `Repository::addCustomSearchPath(<dir>)` 加载（`<dir>/syntax/*.xml`）。
  - 我们自己的 `Repository` 实例没问题；**KTextEditor 自己的 repository** 只能以
    `const KSyntaxHighlighting::Repository &Editor::repository() const` 拿到（`editor.h:280`），
    没有公开注入接口 → 实测只能 `const_cast` 注入（探针结果 `ktexteditor_repo_whitelist_visible=1`）。
  - 也就是：产品里要么接受这个 const_cast，要么改配置。
- **B. `QRC_SYNTAX=OFF` + `NO_STANDARD_PATHS=OFF` + `XDG_DATA_DIRS`（推荐，见 REPORT §5）**
  - 把白名单 XML 装到 `<AppDir>/share/org.kde.syntax-highlighting/syntax/`（纯 XML），AppRun 设
    `XDG_DATA_DIRS=<AppDir>/share`；KTextEditor/katepart 的 repository 会自动加载，无需注入 API。
  - 同样是"库内 0 定义"，只是**查找来源**不同；切换方法：`scripts/versions-ksh.env` 里
    `KSH_NO_STANDARD_PATHS="OFF"`，重跑 `bash scripts/build-ksh-selfbuild.sh`。
  - 注：方案 B 下需要确认 AppDir 内没有别的 `share/org.kde.syntax-highlighting/syntax*` 目录，
    否则会加载到非白名单定义（这是"分销什么"的关键检查）。

## 4. T11 收包后的复验命令（都能在包内/解包目录上跑）

```sh
# 1) 包内 KSyntaxHighlighting 必须是干净的（G1）
python3 evidence/r2-ksh-embedded/scan-embedded-defs.py --expect-clean \
  <AppDir>/usr/lib/libKF6SyntaxHighlighting.so.6

# 2) 不得有 systemverilog.xml 进入资源表（G2）
python3 - <<'PY' <AppDir>/usr/lib/libKF6SyntaxHighlighting.so.6
import re, sys
data = open(sys.argv[1], "rb").read()
texts = [data[o:].decode("utf-16-le", "ignore") for o in (0, 1)]
xml = set()
for t in texts:
    xml.update(re.findall(r"[A-Za-z0-9][A-Za-z0-9._+-]*\.xml", t))
print("xml resources:", len(xml), sorted(xml)[:5]); sys.exit(1 if xml else 0)
PY

# 3) 冒烟程序（T60 扩展）在 AppDir 上跑：库内 0 定义 + 白名单按名字高亮
#    F2 下白名单由 XDG 自动加载（无需 --custom-syntax-dir）；启动环境按 FORM-DECISION C3/C4。
XDG_DATA_DIRS=<AppDir>/usr/share XDG_DATA_HOME=<AppDir>/usr/share \
QT_QPA_PLATFORM=offscreen <AppDir>/usr/bin/vedit-m0-smoke --selfcheck --ksh-selfbuild \
  --expect-empty-syntax --highlight-check --expect-kshlib-prefix <AppDir>/usr/lib \
  --expect-libc-prefix <AppDir>/usr/lib

# 3b) 隔离断言（T61）：白名单之外的任何名字出现即失败
XDG_DATA_DIRS=<AppDir>/usr/share XDG_DATA_HOME=<AppDir>/usr/share \
QT_QPA_PLATFORM=offscreen <AppDir>/usr/bin/vedit-ksh-env-probe --list \
  --expect-names Verilog --forbid-names SystemVerilog,HostLeakProbe,UserLeakProbe

# 4) ABI 自检（可选，与 T60 相同判定）
LD_LIBRARY_PATH=<AppDir>/usr/lib ldd -r <AppDir>/usr/lib/libKF6TextEditor.so.6 | grep -i undefined || echo "(none)"

# 5) tree-sitter CLI（grammar lane 用，不属于运行时）
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
  bash scripts/check-tree-sitter-cli.sh
```

## 5. 已知限制

- 白名单夹具只有 `verilog.xml`，日志里的 `Unable to resolve external include rule for "Comments"`
  是缺依赖定义导致的（高亮仍产出 21 个格式区间）；T16 的白名单请连 `include` 依赖一起带。
- 30 个主题仍内嵌（MIT，无开关）；若要"零主题内嵌"只能改上游 `data/CMakeLists.txt`，不在 T60 做。
- 自建库的大小/哈希可作为收包侧的一个快速身份检查（≈696 KB；发行版 ≈2.0 MB），但**不要**把本次
  sha256 当跨环境常量（构建环境/编译器一变就会变；两次本机全量构建相同只是本环境内的一致性）。
