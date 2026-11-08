## 这批改什么

0.2.1 发布后清出来的**遗留小问题**，以及对 `.flist` 的一处口径统一。四条来自复核报告与各车道的 "bounds"，一条来自你的裁定。**没有新增功能，没有改产品行为（除 `.flist` 那一条，是你明确要求的）。**

### 1. 中文漏译（`active` 徽标 + 又扫出 14 条）

标签页的 `active` 徽标用 `translate("VEdit","active")` 渲染，但目录里**零条目** → 中文界面下一直是英文。顺手做完整扫描，又抓到本批新增但未入目录的 **14 条**：5 条文件对话框过滤器、5 条"指定顶层"文案、2 条快捷键属主标签、上/下一个标签。

`1244 → 1256` 条，**0 unfinished / 0 untranslated**；零丢失证明：ADDED 15 / REMOVED 3（逐条理由）/ **CHANGED 0 / LOST 0**。

### 2. 手册仍在教用户用已删除的扩展名

`USER_GUIDE.md` 三处写 `Filelists (*.f *.F *.flist)`，而交付的过滤器只列 `*.f *.F`。现改为交付真相，并保留诚实注脚：`.flist` 仍可通过 `All Files (*)` 打开为普通文本。

顺带修掉 `handover/03-architecture.md` 里过期的 `docs/BUILD.md:278-287` 指向（该节已移到 §12.5）；全 `docs/` 的 `BUILD.md:<行号>` 引用现为 **0 条**。

### 3. 打包/仓库卫生

- `tests/packaging/hostmatrix.sh` 的默认产物是 `VEdit-0.1.1-x86_64.probe.AppImage` —— **这个文件根本不存在**。改为按 mtime 取最新 `VEdit-*x86_64*.AppImage`，空目录才回退当前版本；用脚本内的真实函数验证过四种情形。
- `.gitignore` 新增显式 `packaging/**/.cache/`（243 MB 构建缓存此前只靠就近规则生效，未来一次 `git add packaging/` 就可能吞进来）；用"临时移除规则 → `check-ignore` 变未命中 → 恢复后命中"证明。
- `help_viewer.h` 注释从旧标题「用户指南」改为「用户手册」。
- `probes/slang-spike/` 入库（84 KB、10 个源码文件；`docs/BUILD.md` 有 4 处引用它，同级探针目录早已被跟踪）。

### 4. 补齐唯一没人补的覆盖缺口：分屏双窗格鼠标

两轮独立复核都点名"分屏行为只有代码级证据"。新增 `vedit-split-pane-mouse-e2e`：真实 shell + 真实 `view.split.horizontal` 两窗格 + **真实鼠标事件打进 KatePart 内部控件**。

**38 检查 / 0 失败，连跑两次**，验到的正是没测过的：气泡跟随鼠标所在窗格（`parentIsActiveView=0` 时也正确）、**400 ms 轮询不抢跨窗格泡泡**、无鼠标切换活动窗格不让气泡跳格、`Ctrl+Alt+点击` 只在该格加光标、`Ctrl+点击` 从该格文档触发定义。**没有发现产品缺陷，因此没有改产品代码。**

### 5. `.flist` 口径统一（按你的裁定：不认）

同一问题原本有三个答案：对话框只列 `*.f *.F`、Rust 解析器只认扩展名 `f`（大小写不敏感）、而**打开规划器自己另有一份表，还认 `.flist`**。

现在规划器改为消费**同一个** `vedit::common::isFilelistPath()`（`src/common/file_dialog_filters.h`），私有表删除。`.flist` 不再被规划/导入为 filelist，`.f`/`.F` 行为不变。

两处连带修正，都如实记录：
- selftest 的 `filelist.plan.flist` **反转并改名** `filelist.plan.flist-not-a-filelist`（日志里若还叫 `...flist PASS`，会与旧的错误结论难以区分）；
- e2e 里用 `/lists/extra.flist` 当第二个 filelist 的 fixture 改为 `extra.F` —— **断言本身一字未改**，只是改用真实扩展名，并顺带覆盖大写拼写；
- `app_shell.h` 最后一处声称会导入 `.flist` 的注释同步修正；`grep -rn "flist" src/` 21 行逐行归类，**没有一行再声称 `.flist` 是 filelist**。

## 验证

五门全绿（`evidence/ux-batch-2/leftovers/gates/`）：

```
rust-gates 0 · qt6-lane 0 (ctest 61/61) · selfcheck 0 (321 + 99 检查)
docs 0 · lrelease 0 (1256 条 / 0 未完成 / 0 未译)
```

`.flist` 那条的定向证据：`PROJECT-MODEL-SELFTEST 153/0`、`VE-FILE-DIALOG-FILTERS-SELFTEST 46/0`、`ctest 2/2`、`-Werror` 干净。

## 这批**没有**包含（都需要你拍板或另开批）

- **FIM 的 Rust 侧错误串仍只有中文**（要通 Rust→Qt 翻译通道）；
- **原生（KDE）文件对话框**未测（交付包内无平台主题插件，形态即 Qt 对话框）；
- **`lint.verible.autofix` 仍是 `TransportPending`**（C ABI 缺字段，属功能缺口）；
- **旧 GTK3 管线是否删除**（`packaging/appimage`、`packaging/tarball`、`scripts/build.sh`）——目前保留并标注"非交付物"；
- 一个**测试夹具**曾临时用 `.flist` 当第二份 filelist（见上），已改正，不是产品缺陷。

## 回归风险

低。改动集中在文档、翻译目录、测试与两个小函数；唯一的**行为变化**是 `.flist` 不再被自动当 filelist —— 这正是你裁定要的，且已有正向/反向断言锁定。
