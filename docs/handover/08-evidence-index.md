# 08 · 证据索引

> `evidence/` 现在有 **31 个目录**，其中 **10 个属于旧的 Rust/GTK 产品**。
> 找证据前先确认自己看的是哪一套 —— 两套的票号还会撞车（旧 T15 与新 T15 完全无关）。

## 1. 属于 **Qt6 新产品**（21 个）

| 目录 | 证明什么 | 关键事实 / 数字 |
| --- | --- | --- |
| `m0-build/` | 固定镜像的事实、锁定包、ELF ISA、构建日志 | 22 个 deb 按 sha256 锁定；指纹 `c97a2566…`；`REBUILD-INCIDENT.md` 记录了证据被删后的重建 |
| `m0-runtime/` | 私有运行时打包（自带 loader/glibc/Qt6/KF6/KTextEditor/KSH/tree-sitter） | `F2.md` 记录了两次"沉默的假绿"；交付 AppImage 65,673,720 B / `74bd6aae…` |
| `m0-hostmatrix/` | 四宿主矩阵 | A0a CentOS 7.9（glibc 2.17）/ A0b Ubuntu 24.04 / A0d base-only（宿主 libQt6=libKF6=libX11=libfuse2=0）/ A0e 无 GPU 无 GL；窗口可见性 `compare -metric AE = 307200` |
| `m0-ksh-selfbuild/` | 自建 KSyntaxHighlighting 的形态决策（**F2**）与冒烟验证 | `FORM-DECISION.md`（F2 + C1–C8）；`SMOKE-BINARY-IDENTITY.md` |
| `r2-license/` | grammar 身份/许可 + C7 `.sv` 处置 | `verify.sh` **51 passed / 0 failed**，含 3 条反向对照；`GATES.md` |
| `r2-ksh-embedded/` | KSyntaxHighlighting 把定义编进 `.so` 的事实 | 450 个内嵌 XML 名、387 个可提取、**36 个严格 GPL**（SystemVerilog = GPLv3+） |
| `r2-c15/` | C15 接缝探针（抽象 provider + 编译期 grammar 选择） | 四配置换装 |
| `r2-c17/` | C17 overlay 探针（宿主侧 range/attribute 接口） | v1 + **v2**（官方锁定镜像复跑）；性能数字见 v1/v2 对照（v1 曾低估 1.2–1.7×） |
| `r2-fold/` | 折叠接口能否被外部 range 驱动 | 结论进 AMENDMENTS A1 |
| `syn-provider/` | provider 库自测与结论 | `07-conclusion.md`；ctest |
| `syn-kde-data/` | KSH 私有数据路径加载 + 降级 | `00-load.log` / `02-degradation.log` |
| `syn-grammars/` | tree-sitter grammar 绑定 + 三组 query | 四配置构建；三个带 grammar 变体各 **79/79**（237 PASS / 0 FAIL） |
| `app-shell/` | 应用外壳装配与自测 | 73 项 selftest + 4 张 PNG；`06-seams.md` 记录接缝；⚠️ 自检时序有缺陷（G6） |
| `ws-filetree/` | 文件树面板 | `REPORT.md`；⚠️ dock 占位是否真替换无 verdict |
| `ws-palette/` | 命令面板 | 10k 文件 200ms 基准 |
| `ws-search/` | 全局搜索 | `REPORT.md` |
| `term-panel/` | 终端（自写 pty + VT） | 101 项 CHECK；`00-pins.txt` 证明镜像里既无 qtermwidget 也无 libvterm |
| `edit-core/` | 编辑内核**设计** + 三个探针 | `DESIGN.md`；`00-probe.md`（147 行，含四条硬约束、A1b 口径、两条被推翻的假设） |
| `build-registration/` | 根构建登记：反例（红）+ 真构建（绿）+ target + 链接 | `00-counterexample.txt` exit 1；`01-build.txt` exit 0 + ctest 9/9；`03-link.txt` 用 `ninja -t commands` / `nm`（267 个 `vedit::terminal::` 符号） |
| `t63-root-cmake-lanes/` | 根 CMake 纳入 lane 目录 + 反例验证 | 证明"未编译却绿"能被抓出 |
| `tree-cleanup/` | 本轮工作树收口 | 16 G → 392 M；`REPORT.md`；收口后 `SELFCHECK PASS` |

## 2. 属于 **旧的 Rust/GTK 产品**（10 个）

> 这些**不是**新产品的证据。它们对应已发布的 0.1.x GTK 版本，票号是旧看板的。

| 目录 | 旧产品里的东西 |
| --- | --- |
| `appimage/` | 旧产品的自包含 AppImage 证据 |
| `config/` | 配置与工程发现（旧 T13） |
| `highlight/` | 语言识别与高亮（旧 T11） |
| `lsp/` | Verible LSP 客户端 lane（旧 T15） |
| `fim/` | 可选的 FIM 补全（旧 T19） |
| `ime/` | 容器内中文输入证明（AC-16） |
| `github/` | Issue #1/#2 的复现、修复与独立验证 |
| `release/` | 0.1.1 版本号提升 |
| `release-verification/` | 已发布字节确实能跑（`47,745,528` B / `e4317bc3…` —— 注意这是**旧产品**的 AppImage，与新产品的 `74bd6aae…` 不是同一个东西） |
| `wiring/` | 旧产品的接线证据、套件、截图 |

## 3. 怎么快速判断该看哪一套

- 看到 `cargo` / `crates/` / `gtksourceview` / `floem` → 旧产品。
- 看到 `KTextEditor` / `KSyntaxHighlighting` / `AppImage 私有运行时` / `tree-sitter` / Qt6 版本号 → 新产品。
- 新产品的一切最终证据都在 `docs/plans/v2-slices.md` 与 `docs/handover/02-status-matrix.md` 的框架下解释。

## 4. 证据纪律（与 `05-build-and-verify.md` §2 一致）

1. 每条证据文件头写清采集时间 + `HEAD` + `git status` 干净度。
2. 「构建通过」必须附 target 清单 + 一次证伪。
3. 报告正文里的数字必须由脚本产出并指向证据文件，不许手抄。
4. 删证据目录前先 `git ls-files`；未入库的证据目录禁止 `rm -rf`。
