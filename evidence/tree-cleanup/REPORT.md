# 工作树收口（2026-10-31）

用户指令：**「清理当前工作树，重新整理并继续计划」**，随后补充 **「把 cellwork 先给我停了」**。

## 结论

| 项 | 前 | 后 |
| --- | --- | --- |
| 工作树体积 | 16 G | **392 M**（−96%） |
| 未跟踪文件 | 1,017 个 / 238 M | **0** |
| `git status` | 50+ 条脏 | **干净** |
| 交付物 | — | 保留并校验一致 |

## 这轮最重要的发现

**整个 Qt6 迁移从来不在版本控制里。** 仓里 86 个提交全是被替换掉的 Rust/GTK 产品；`src/`、`cmake/`、
`scripts/`、`evidence/`、`packaging/runtime/` 全是未跟踪文件。也就是说，在这次收口之前，一周的工作
只存在于一块硬盘上 —— 而 `evidence/m0-build/` 在这之前已经被误删过一次。

补救：从 `dev` 切出 `feat/qt6-ktexteditor-migration`，把 1,017 个文件分六个提交入库
（`build` 工具链 → `feat` 六条 lane 源码 → `docs` 许可与构建协议 → `test` 各 lane 证据 →
`chore` 忽略规则 → `docs(plan)` 切片计划）。`dev` 未被污染。

## 删掉的 15.6 G（全部可重建，明细见 `02-removals.txt`）

`target/` 13 G、`build/gui-spike` 的 cargo target 1.4 G、旧产品打包暂存 380 M、
AppDir 暂存 342 M、解包的镜像根 171 M、FIM harness target 115 M、slang 缓存与探针构建 118 M。

## 明确保留（唯一副本，不可从树重建）

`packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage` —— 65,673,720 B，
sha256 `74bd6aae2af9aefc8a2200d5ff66b1868c3e8281fb77de21f7859c5cafb33faa`。
四宿主矩阵的证据全部指向这个路径。收口后重跑自检：

```
bash packaging/runtime/selfcheck.sh --appimage "$PWD/packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage"
→ SELFCHECK PASS        （全文见 01-deliverable-selfcheck.log）
```

其中 `libc` 仍从包内解析、`KSH-GUARD PASS whitelist=SystemVerilog,Verilog`、
`strict-GPL definitions embedded: 0`。**清理没有伤到交付物。**

## 两次误操作，都已修复并留痕

1. **`build/gui-spike` 其实是已入库目录**。`rm -rf` 连带删掉 69 个 tracked 文件
   （源码、docker 文件、它的 evidence）。用 `git checkout -- build/gui-spike` 全量恢复，
   现在 800 K，只丢了它自己的 cargo target —— 那本来就是要删的。
   教训：删任何目录前先 `git ls-files <path>`，确认它是不是已在库里。
2. **容器以 `nobody:nogroup` 0555 写死的文件宿主删不掉**（`target/ksh-selfbuild`、
   `target/ws-filetree-perm`，共 151 M），宿主没有 sudo（`no new privileges` 标志）。
   解法：`docker run --rm -v "$PWD:/w" vedit-bx-probe:local sh -c 'rm -rf /w/target/…'`。

## 编排模式变更

`cellwork_stop force:true` 强制归档 `cell-20261031T014507403Z`（当时仍有 5 张 in_progress：
T14/T18/T21/T22/T65），journal 记录了强制原因，一件东西没删。
13 个成员的会话仍在（`send_message` 可唤醒，各自上下文保留），但**看板不再是派工依据**；
执行依据改为 `docs/plans/v2-slices.md`，由 captain 直管派活。
