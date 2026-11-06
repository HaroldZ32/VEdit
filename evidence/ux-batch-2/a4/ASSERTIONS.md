# A4 验收断言 ↔ 测试行对照

命令（全部 offscreen，容器内）：

```
# 1) 设置 lane 的自测（含 ①-⑤ 与 ⑥ 的产品路径）
QT_QPA_PLATFORM=offscreen target/qt6-build/src/app/workspace-lane-settings/vedit-workspace-settings-selftest --selfcheck
# 2) ⑥ 的专用 ctest 行（只跑重启路径）
cd target/qt6-build && ctest -R "vedit-ui-scale-restart|vedit-workspace-settings-selftest" --output-on-failure
```

上一次观测：`SETTINGS-SELFTEST ok=1 checks=228 failures=0`（其中 `settings.a4.*` 121 条）；
`ctest` 两行均 Passed（1.42 s / 0.24 s）。原始输出见 `logs/10-settings-selftest.log`、
`logs/11-ui-scale-restart.log`、`logs/14-full-ctest.log`。

| 验收项（计划 §3.A4） | 断言行（CHECK 名前缀） | 证据 |
|---|---|---|
| ① settings 读写幂等 | `settings.a4.format.restart-values`、`settings.a4.format.idempotent`（同一份值再存一次，`settings.toml` 逐字节相同）、`settings.a4.format.single-key`（每个键只出现一次）、`settings.a4.lint.restart-values`、`settings.a4.lint.idempotent` | `logs/10-settings-selftest.log` |
| ② format argv 与选项一一对应（含"默认不传 flag"） | `settings.a4.format.default-values` / `default-argv`（默认 → argv 为空）、`settings.a4.format.knobs`（23/23 旋钮都有控件）、`settings.a4.format.argv-one-to-one`（23 个值 → 23 个 flag，顺序=表序）、`settings.a4.format.argv-count`、`settings.a4.format.flags`（逐字校验 `--column_limit=120` 等）、`transport-runs` / `transport-argv` / `transport-default`（**真实** `formatVerilog` 调一个假 formatter，回显它收到的 argv：默认只有 `-`） | `logs/10-settings-selftest.log`、`logs/20-format-argv-before-after.txt` |
| ③ 两套 lint 配置互不覆盖 | `settings.a4.lint.two-sets`（同一文件里 `lint.verible.*` 与 `lint.slang.*` 并存）、`settings.a4.lint.switch-back-slang`、`switch-back-verible`、`no-crosswrite`（只比较 `lint.*` 行：切引擎后逐字节不变） | `logs/10-settings-selftest.log` |
| ④ 不支持的旋钮 disabled + 原因 | `settings.a4.lint.slang.ruleset-disabled`（5 个 verible 旋钮全 disabled）、`slang.ruleset-reason`/`config-reason`/`waiver-reason`/`autofix-reason`（各自的确切原因文本）、`verible.slang-disabled` + `verible.slang-reason`、`state.backend-unsupported` 与 `state.transport-pending` / `state.supported`（两类"不可用"在状态表里分开）、`verible.status-label` / `slang-status-label`（页面上的可见说明） | `logs/10-settings-selftest.log`、`screenshots/05-lint.png` |
| ⑤ 字体即时改变 `QApplication::font().pointSize()` | `settings.a4.font.applied`（17）、`settings.a4.font.qapplication`、`settings.a4.font.widget`（通过控件设 13 → `QApplication::font().pointSize()==13`）、`font.restart-applied`（重启后 13）、`font.system`（回到启动字号）、`font.product-line`（真实二进制 `font_point_size=19`） | `logs/10-settings-selftest.log` |
| ⑥ scale 设置写入后重启路径发出正确 `QT_SCALE_FACTOR` | `settings.a4.scale.env-100/125/150/200/invalid`（纯函数：100 不设变量、125→1.25、150→1.5、200→2、非法值不改动）、`scale.product-line`（真实 `vedit-app --ui-scale-echo`：`qt_scale_factor=1.5`）、`scale.product-default-line`（100 → `<unset>`）、`scale.default-line` / `default-path`（**无 `--state-dir`** 的普通启动：`XDG_CONFIG_HOME/VEdit/vedit-app/settings.toml` → `2`）、`scale.default-empty`（没有 settings 文件 → 100/unset） | `logs/10-settings-selftest.log`、`logs/11-ui-scale-restart.log`、`logs/12-screenshots.log` |
| 证据含 offscreen 前后对照 + 截图 | 前后对照 = `logs/20-format-argv-before-after.txt`（默认空 argv ↔ 全量 argv，且真实进程收到同一份）；截图 = `screenshots/01..06-*.png`（六页，Format 页含展开的高级区） | 同左 |

## 反例/负控制
- 默认不传 flag：`settings.a4.format.default-argv` 为空 + `transport-default` 真实进程只收 `-`。
- 默认不留痕：`settings.a4.font.system-not-stored`、`settings.a4.scale.default-cleared`、`settings.a4.format.restored`（`settings.toml` 里不含 `format.`）。
- 非法值降级：`settings.a4.read.degrades`（`ui.scale_percent=1337` → 100、`font_point_size=999` → 跟随系统）。
- 旧文件不被破坏：`settings.a4.legacy.preserved` / `legacy.no-new-keys` / `legacy.no-diagnostics`。
