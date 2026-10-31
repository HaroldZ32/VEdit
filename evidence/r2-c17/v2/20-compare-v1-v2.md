# C17 v2：官方构建镜像复跑 vs v1 探针镜像 —— 对照表

- Board task: **T59** · 契约: `.mpd/cellwork/cell-20261031T014507403Z/contracts/r2-c17-probe.md`
- 需求: `requirements.md` rev 2（C17 / A1g / A1h）
- v1（**未改动**，sha256 前后一致见 `17-v1-untouched-verdict.txt`）: `evidence/r2-c17/结论.md` + `00-pins.txt`…`03-artifacts.txt` + PNG
- v2（本目录）: `evidence/r2-c17/v2/`，一键重跑 `bash evidence/r2-c17/v2/run-v2.sh`
- v2 在同一官方镜像上背靠背跑了两次：**v2A** = 本目录，**v2B** = `v2/rerun/`（用来量化运行间噪声）

## 0. 变更点（先读这一节）

**M1（方法学，最重要）v1 §5 的整窗 grab 绝对 p50 不能当基线，只能当量级。**
同一官方镜像背靠背两次复跑，整窗 grab p50 自己就漂：plain 基线 1506.53 → 1616.26 µs（+7.3%），
Verilog 基线 2049.95 → 1771.83 µs（−13.6%）；两次「同日志 p50 相减」的差值相差 **28%–67%**
（见 §5）。v1 的 1385 / 1878.81 µs 与 v2 的差距（plain 比 v2A 低 8.0%；Verilog 落在两次 v2 之间）
与这一散布同量级。
→ 跨会话的 grab 绝对值（含 v1 的）不应写进验收阈值或回归基线；C17 的性能判据应继续用
§3 的**注入成本**（µs 级、稳定、远低于帧预算）。

**M2（方向，略偏坏，但不改变结论）注入 p50 比 v1 略高。**
6 项注入里 v2A 比 v1 高 **1.9%–13.6%**（5 项 >10%），v2B 高 **3.7%–9.5%**；而同镜像两次复跑的散布
本身就有 **1.7%–7.2%**。→ 方向一致（略慢），幅度在噪声带内/边缘，**不能归因于镜像**
（两镜像的 `.so` 与头文件逐字节相同，见 M3）。
最差 p95 = 122.853 µs（v2B plain per-token）= 16.7 ms 帧预算的 **0.74%**；
最差单帧 max = 149.647 µs（v2B Verilog per-token）= **0.90%**（v1 自己的最大单帧是 156.07 µs / 0.94%）。
「可以增量」的判定不变。

**M3（假设被排除）两张镜像的被测栈逐字节相同。**
KTextEditor / KSyntaxHighlighting 的公共头与 `.so` sha256 相同、Mesa 26.0.8-1ubuntu0.3、
glibc 2.43-2ubuntu2.4、Qt 6.10.2+dfsg-7、KF6 6.24.0 全部同版本（`19b-headers-and-so-compare.txt`、
`19c-render-stack-compare.txt`）。→ v1↔v2 的任何数字差异都**不可能来自库实现**，
只能来自宿主负载/调度（v1 采集时未记录宿主 load，这是 v1 的方法缺陷，v2 已记录：load(1m/5m/15m)=4.83/5.21/4.16，16 核）。

**M4（未变，关键）所有确定性与像素级结论逐位一致。**
1024/行上限（1000→`103568` px、1100→`0` px）、`setDefaultStyle` 在默认内建主题下不生效
（`347→347`）、应用侧解析主题色可用（87 个精确同色像素）、20/20 `CHECK` PASS、
无文档级 range 枚举 / 无逐点属性查询 / 无公共语义 API（`19-negative-api.txt`，含正对照）。
v1 与 v2 的这些行在日志里的**行号都相同**（v1 `02-run.log:46-59` = v2 `02-run.log:46-59`）。

**M5（诚实标注）探针脚本未改。** `probes/c17-overlay/scripts/run-probe.sh` 的 sha256 前后一致
（`16-run-probe-sha.txt`）。因此 `v2/00-pins.txt` 里那行 `image: vprobe-qt6kf6:1` 是脚本**硬编码**的
v1 字符串（`run-probe.sh:23/27`），对 v2 是**错的**；v2 的权威身份见 `00-image-identity.txt`
与 `12-image-pin-assertions.txt`。这是「不改探针实现」这条铁律的直接代价，已如实标注。

## 1. 镜像身份：用锚点，不用 image id

| 锚点 | v2 实测 | 证据 |
|---|---|---|
| 锁定输入 sha256 | `versions-qt.env` `bb510b35…` / `qt6-locked-packages.txt` `cbfa9b42…` / `Dockerfile.build` `987492fd…` | `10-pins-anchor.txt` |
| 组合指纹（三文件拼接） | **`c97a25661f10434eb4c592be3e701d2847d0ad8d1995249c571cdc1d720c6fa4`**（与 T10 一致） | `10-pins-anchor.txt` |
| 锁文件条目数 | **22** | `10-pins-anchor.txt` |
| 容器内 22 包 dpkg 断言 | `checked=22 mismatches=0` | `12-image-pin-assertions.txt` |
| 镜像自带包快照 == T10 证据 | `/opt/vedit/package-snapshot.txt` `04887dd0…` == `evidence/m0-build/build-logs/30-qt6-package-snapshot.txt` | `12-image-pin-assertions.txt` |
| 镜像内锁文件副本 == 仓库副本 | `cbfa9b42…` == `cbfa9b42…` | `12-image-pin-assertions.txt` |
| RootFS 层指纹（内容寻址） | 本镜像 `fcd4b231…` == `vedit-qt6-build:t10-evidence` `fcd4b231…`（文件系统逐字节相同） | `11-image-locator.txt` |
| 冒烟锚点 | `M0-SMOKE ok=1 platform=xcb qt=6.10.2 kf6=6.24.0 … definitions=451 … custom_path=1` | `13-m0-smoke.log` |

**为什么不能拿 image id 当判据：** 同一 tag 在本次会话期间就换了 id——T10 自己落盘的
`evidence/m0-build/build-logs/21-qt6-image-id.txt`（11:39）记录 `de2b7f70…`，本次
`11-image-locator.txt`（11:52）记录 `ba08930b…`，两者 `created` 都是 `11:14:08.85`；
终端里还观察到 `cf2ca614…`、`2548f11d…`（未单独落盘）。同时
`vedit-qt6-build:t10-evidence`（`eb35fba1…`）与本次镜像的 **RootFS 层指纹相同**
（`fcd4b231…`）——同一 Dockerfile 的不同 image 记录可以共享完全相同的文件系统。
因此 `run-v2.sh` 在开始时**只解析一次** tag→id，之后每一步（断言/阴性查证/冒烟/两次探针）
都用同一个 id 执行，避免中途换镜像；id 只作定位，不作判据。

## 2. 环境差异

| 项 | v1 探针镜像 `vprobe-qt6kf6:1` | v2 官方构建镜像 `vedit-qt6-build:local` | 判定 |
|---|---|---|---|
| 发行版 | Ubuntu 26.04 | Ubuntu 26.04.1 LTS | 同代 |
| Qt | 6.10.2+dfsg-7 | 6.10.2+dfsg-7 | 同 |
| KF6 / KTextEditor / KSyntaxHighlighting | 6.24.0-0ubuntu1 | 6.24.0-0ubuntu1 | 同 |
| tree-sitter | 0.25.9-7 | 0.25.9-7 | 同 |
| glibc | 2.43-2ubuntu2.4 | 2.43-2ubuntu2.4 | 同 |
| Mesa / llvmpipe | 26.0.8-1ubuntu0.3 | 26.0.8-1ubuntu0.3 | 同 |
| KTextEditor 公共头 / `.so` | — | — | **逐字节相同**（`19b`） |
| 镜像来源 | 自建替代镜像（`probes/c17-overlay/image/Dockerfile`，基础镜像按 tag） | T10 官方构建镜像（基础镜像按 digest + 22 包 sha256 锁定） | v2 是正式锚点 |
| 宿主负载记录 | 无 | `load≈4.8–5.3`（16 核，`10-pins-anchor.txt` 与 `run-v2.sh` 输出） | v2 可归因 |

## 3. 确定性结论对照（逐位）

| 指标 | v1 | v2A | v2B | 判定 |
|---|---|---|---|---|
| `overlay_foreground_px`（叠加前景像素） | 71 | 71 | 71 | 一致 |
| `overlay_background_px`（叠加背景像素） | 325 | 325 | 325 | 一致 |
| `baseline_band_colored_px`（叠加前同带饱和像素） | 183 | 183 | 183 | 一致 |
| `overlay_band_base_keyword_px`（叠加后底色残留） | 12 | 12 | 12 | 一致 |
| 删除 overlay 后 `restored_band_base_keyword_px` | 30 | 30 | 30 | 一致 |
| 删除后 `restored_band_overlay_foreground_px` | 0 | 0 | 0 | 一致 |
| 语义：baseline / defaultstyle_hint 饱和像素 | 347 / 347（不生效） | 347 / 347 | 347 / 347 | 一致 |
| 语义：应用侧解析主题色后的精确同色像素 | 87 | 87 | 87 | 一致 |
| **1024 上限**：1000 range/行 标记像素 | `103568` | `103568` | `103568` | 一致 |
| **1024 上限**：1100 range/行 标记像素 | `0`（整行静默丢弃） | `0` | `0` | 一致 |
| `CHECK` 条数 / PASS | 20 / 20 | 20 / 20 | 20 / 20 | 一致 |
| `RESULT` | `PASS failures=0` | `PASS failures=0` | `PASS failures=0` | 一致 |
| 阴性 API：`movingRanges` / `attributesAt` / `semantic` | exit=1 无匹配（T58 补录） | exit=1 无匹配 + 正对照 exit=0 | — | 一致（v2 在官方镜像重做，`19-negative-api.txt`） |

## 4. 性能对照（p50 / p95，单位 µs）

### 4.1 plain（无底座语法高亮，200 帧，120,001 行）

| 策略 | v1 p50 / p95 | v2A p50 / p95 | v2B p50 / p95 |
|---|---|---|---|
| `grab_baseline_no_overlay` | 1385 / 1786.35 | 1506.53 / 1924 | 1616.26 / 2167.8 |
| `full_recreate_per_line.inject` | 16.717 / 27.3105 | 18.621 / 29.016 | 18.31 / 28.85 |
| `full_recreate_per_line.grab_with_overlay` | 1849.58 / 2269.24 | 1900.35 / 2270.15 | 1883.55 / 2314.91 |
| `full_recreate_per_token.inject` | 66.864 / 107.217 | 68.111 / 102.131 | 69.659 / 122.853 |
| `full_recreate_per_token.grab_with_overlay` | 2112.04 / 2912.08 | 2135.7 / 2415 | 2135.63 / 2530.57 |
| `incremental_pool.inject` | 8.421 / 18.7 | 9.2875 / 19.053 | 8.922 / 19.7475 |
| `incremental_pool.grab_with_overlay` | 1810.39 / 2208.92 | 1832.13 / 2142.49 | 1817.21 / 2301.98 |

### 4.2 Verilog（底座 Verilog 高亮同时开启，120 帧）

| 策略 | v1 p50 / p95 | v2A p50 / p95 | v2B p50 / p95 |
|---|---|---|---|
| `grab_baseline_no_overlay` | 1878.81 / 2875.9 | 2049.95 / 2960.5 | 1771.83 / 2183.2 |
| `full_recreate_per_line.inject` | 17.674 / 25.6725 | 19.888 / 27.9795 | 18.455 / 25.4122 |
| `full_recreate_per_line.grab_with_overlay` | 2479.14 / 2823.71 | 2645.15 / 3082.7 | 2534.44 / 2947.56 |
| `full_recreate_per_token.inject` | 74.4125 / 113.738 | 83.0195 / 122.398 | 77.178 / 111.91 |
| `full_recreate_per_token.grab_with_overlay` | 2767.06 / 3554.46 | 2916.73 / 3667.79 | 2816.31 / 3276.6 |
| `incremental_pool.inject` | 6.538 / 16.5135 | 7.429 / 16.6875 | 6.8935 / 16.3551 |
| `incremental_pool.grab_with_overlay` | 2500.65 / 3009.19 | 2490.67 / 2965.79 | 2507.09 / 2980.27 |

## 5. 按 v1 §5 的方法复算「同日志 p50 相减」

同一份日志内 `overlay 的 p50 − 同日志 baseline 的 p50`（不是跨日志、不是跨会话）。
原始行号：v1 plain `02-run.log:64/68/73`、Verilog `02b-run-verilog.log:10/14/19`；
v2A/v2B 的行号完全相同（探针输出形状未变）。

| 运行 | 模式 | 策略 | baseline | overlay | 差值 | 百分比 |
|---|---|---|---|---|---|---|
| v1 | plain | per_line | 1385.00 | 1849.58 | +464.58 | +33.5% |
| v1 | plain | incremental | 1385.00 | 1810.39 | +425.39 | +30.7% |
| v1 | plain | per_token | 1385.00 | 2112.04 | +727.04 | +52.5% |
| v2A | plain | per_line | 1506.53 | 1900.35 | +393.82 | +26.1% |
| v2A | plain | incremental | 1506.53 | 1832.13 | +325.60 | +21.6% |
| v2A | plain | per_token | 1506.53 | 2135.70 | +629.17 | +41.8% |
| v2B | plain | per_line | 1616.26 | 1883.55 | +267.29 | +16.5% |
| v2B | plain | incremental | 1616.26 | 1817.21 | +200.95 | +12.4% |
| v2B | plain | per_token | 1616.26 | 2135.63 | +519.37 | +32.1% |
| v1 | verilog | per_line | 1878.81 | 2479.14 | +600.33 | +32.0% |
| v1 | verilog | incremental | 1878.81 | 2500.65 | +621.84 | +33.1% |
| v1 | verilog | per_token | 1878.81 | 2767.06 | +888.25 | +47.3% |
| v2A | verilog | per_line | 2049.95 | 2645.15 | +595.20 | +29.0% |
| v2A | verilog | incremental | 2049.95 | 2490.67 | +440.72 | +21.5% |
| v2A | verilog | per_token | 2049.95 | 2916.73 | +866.78 | +42.3% |
| v2B | verilog | per_line | 1771.83 | 2534.44 | +762.61 | +43.0% |
| v2B | verilog | incremental | 1771.83 | 2507.09 | +735.26 | +41.5% |
| v2B | verilog | per_token | 1771.83 | 2816.31 | +1044.48 | +58.9% |

（表格原文：`20-deltas.txt`，由 `scripts/extract-metrics.sh` 产出的 `20-metrics.tsv` 计算。）

**读法：**

- v2 的 12 个差值（3 策略 × 2 模式 × 2 次运行）区间是 **+12.4% ~ +58.9%**；**v1 的四个数（+30.7 / +33.5 / +32.0 / +33.1）全部在该区间内**，未被推翻。
- 同镜像两次的差值本身相差 **28%–67%**（如 plain per_line：+393.82 vs +267.29），
  说明「两个 p50 相减」比 p50 本身噪声大得多；**任何 ±30% 以内的差值都不该当结论引用**。
- 方向性结论稳定：**整窗渲染是主要成本（1.4–2.9 ms 量级），overlay 只追加约 +1 到 +7 百 µs**；
  而真正的注入成本只有 7–83 µs（p50），增量池最低。

## 6. 哪些结论在官方镜像上仍成立 / 哪些变了

**仍成立（v2 逐位或逐字节复现）：**

1. 宿主侧 range / attribute overlay 接口存在且渲染生效、可查询、可删除回退、随文档移动（20/20 CHECK PASS）。
2. 每行属性 range 上限 **1024**，1100→整行静默丢弃（像素数逐位一致）。
3. 无文档级 range 枚举、无逐点属性查询、无公共语义高亮 API（官方镜像上重做阴性查证 + 正对照）。
4. `Attribute::setDefaultStyle` 在**默认内建主题**下不参与渲染；应用侧用
   `Theme::textColor()` 解析后显式设色可用（347/347、87 px 逐位一致）。
5. 注入成本远低于 16.7 ms 帧预算，**可以增量**（最差 p95 122.853 µs = 0.74%；最差单帧 149.647 µs = 0.90%）。
6. 整窗渲染是主成本，overlay 追加量级为数百 µs（+12%–59% 区间，v1 的 +30%–33% 在其中）。

**变了（仅数字层面，不改变结论）：**

1. 整窗 grab 的**绝对 p50 跨会话不可复现**（同镜像两次就差 7%–14%；v1 的 1385/1878.81 落在该散布内）。
   → 不要把这些绝对数写进验收阈值；用同日志相对量或注入成本。
2. 注入 p50 的绝对值比 v1 高 1.9%–13.6%（v2A）/ 3.7%–9.5%（v2B），同镜像两次相差 1.7%–7.2%；方向略偏慢，幅度在噪声带内/边缘。
3. v1 §5 的「+464.58 µs ≈ +33.5%」这类数字**仍然能与日志对上**，但它的**精度**被高估了：
   同一方法在 v2 上给出 +12%–59% 的散布。

**未测（与 v1 相同，v2 未扩大）：** 具名主题/用户配色下的 `setDefaultStyle`；
zDepth 的像素级优先级；屏上局部重绘成本；4K/HiDPI/多视图/字体切换。

## 7. 复跑命令

```bash
# 一条命令重跑 v2 全部证据（不写 v1；会重建 probes/c17-overlay/build）
bash evidence/r2-c17/v2/run-v2.sh
```

内部顺序：锁锚点 → 镜像 tag 解析一次成 id → 容器内 22 包断言 → 阴性 API 查证 →
M0 冒烟（挂载 /vedit-build）→ `scripts/run-probe.sh`（**未修改**）写 `v2/` →
同一 id 再跑一次写 `v2/rerun/`（噪声带）→ 校验 v1 未被改动。
逐步日志：`run-v2.sh` 的 stdout；探针自身的输出：`14-console.log` / `14b-console-rerun.log`。

## 8. 证据索引

| 文件 | 内容 |
|---|---|
| `00-image-identity.txt` | 实际镜像 + 为什么 `00-pins.txt` 里的 image 标签是硬编码旧串 |
| `00-pins.txt` / `01-build.log` / `02-run.log` / `02b-run-verilog.log` / `03-artifacts.txt` / `09-result.txt` | 探针按原脚本产出的全部证据（10 张 PNG 同目录） |
| `rerun/` | 同一镜像第二次运行（噪声带） |
| `10-pins-anchor.txt` | 锁文件 sha256、组合指纹 `c97a2566…`、22 条 |
| `11-image-locator.txt` | 解析的 image id（仅定位）、RootFS 层指纹、与 T10 证据 tag 相同 |
| `12-image-pin-assertions.txt` | 容器内 22 包逐条断言 + 包快照比对 + 探针依赖 |
| `13-m0-smoke.log` | `M0-SMOKE ok=1 … qt=6.10.2 kf6=6.24.0` |
| `14-console.log` / `14b-console-rerun.log` | 两次 `docker run … run-probe.sh` 的完整 stdout |
| `15-probe-exit.txt` / `15b-probe-exit-rerun.txt` | 两次退出码（均 0） |
| `16-run-probe-sha.txt` | 探针脚本 sha256 前后一致（未修改） |
| `17-v1-untouched-*.txt` | v1 顶层文件 sha256 前后一致（未被覆盖） |
| `18-v1-probe-build-manifest.txt` | 每轮开始前的探针构建产物 sha256/文件数（随后删除以强制在官方镜像内重建；文件含诚实说明） |
| `19-negative-api.txt` | 官方镜像上的阴性 API 查证（含正对照） |
| `19b-headers-and-so-compare.txt` | 两镜像公共头与关键 `.so` 逐字节相同 |
| `19c-render-stack-compare.txt` | 两镜像 Mesa/glibc/Qt/KF6 版本相同 |
| `20-metrics.tsv` / `20-deltas.txt` | 机械提取的对照数据（本文表格的原始数据） |
