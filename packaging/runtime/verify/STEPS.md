# 在 CentOS 7 真机上验收 VEdit AppImage — 3 分钟操作说明

这份说明只讲“怎么做”，不需要读代码。你只需要把一个**脚本**和一个 **AppImage** 拷到 VM 上，
跑一条命令，然后把生成的**报告文件**整份发回。

---

## 1. 需要拷到 VM 上的文件

| 文件 | 在哪 | 必须吗 |
|---|---|---|
| `VEdit-0.2.2-x86_64.AppImage` | 仓库 `packaging/runtime/out/` | **必须**（交付物本体） |
| `verify-appimage-centos7.sh` | 仓库 `packaging/runtime/verify/` | **必须**（验证脚本） |
| `SHA256SUMS` | 与 AppImage 同目录 | 可选（有的话会多一条交叉校验；没有也能跑） |

就这两个主文件（`SHA256SUMS` 可选），**不需要**拷整个 AppDir、不需要工具链、不需要样例工程。

在**你自己的电脑**上执行（把 `<VM_IP>`、`<用户名>` 换成你的）：

```bash
cd <仓库目录>/packaging/runtime/out
scp VEdit-0.2.2-x86_64.AppImage SHA256SUMS <用户名>@<VM_IP>:~/
cd ../../../packaging/runtime/verify     # 注意：脚本在 verify/ 下
scp verify-appimage-centos7.sh <用户名>@<VM_IP>:~/
```

> 懒得 cd 的话，两个文件在仓库里的**绝对路径**分别是：
> `packaging/runtime/out/VEdit-0.2.2-x86_64.AppImage` 和
> `packaging/runtime/verify/verify-appimage-centos7.sh`。

文件大小与 sha256 **以同目录的 `SHA256SUMS` 为准**：脚本会读取该行并核对
（传坏了会直接报 `appimage.sha256` / `appimage.sha256sums` 失败）。如果要与别处记录的固定值对照，
可用 `VEDIT_EXPECTED_SHA256` / `VEDIT_EXPECTED_SIZE` 传入。

---

## 2. 在 VM 上跑（一条命令）

```bash
cd ~
chmod +x VEdit-0.2.2-x86_64.AppImage verify-appimage-centos7.sh
bash verify-appimage-centos7.sh
```

- 不需要 `sudo`，不需要 `yum install`，不需要联网。
- 大约 20–60 秒（要解一次包、启动几次程序）。
- 屏幕上会逐条打印 `RESULT PASS|FAIL|SKIP …`，最后一行是：

```
SUMMARY: PASS=16 FAIL=0 SKIP=0
Report: /home/<你>/verify-report-<主机名>-<年月日-时分秒>.txt
```

**真机（CentOS 7，内核 3.10）应有的结果**：
- 拷了 `SHA256SUMS`：`PASS=16 FAIL=0 SKIP=0`；
- 没拷 `SHA256SUMS`：`PASS=15 FAIL=0 SKIP=1`（SKIP 的那条就是“可选的 SHA256SUMS 不存在”，不算失败）。
- 如果 `kernel.is-3.10` 不是 PASS，先确认你真的在 VM 里跑而不是在容器里（见 §5）。

---

## 3. 把报告发回

脚本会在**当前目录**生成 `verify-report-*.txt`（里面是全部 `RESULT` 行 + 程序自己的完整自检输出 +
手工栏）。**把这个文件整份发回**即可（几万字节，纯文本）：

```bash
ls -l verify-report-*.txt
# 用你习惯的方式拉回去，例如在你自己的电脑上：
# scp <用户名>@<VM_IP>:~/verify-report-*.txt .
```

---

## 4. 失败了怎么看（只看这几行）

报告里每条检查是一行，格式固定：

```
RESULT PASS|FAIL|SKIP <检查名> | expected=… | actual=… | note=…
```

- 只要 `SUMMARY` 里 `FAIL=0` 就是通过（`SKIP` 不影响通过，但要看 note 里的原因）。
- 有 `FAIL` 时，直接看那一行的 `actual=` / `note=`：
  - `appimage.sha256` FAIL → 文件传输坏了/被改过：**重新拷一次** AppImage（脚本里内置了正确的 sha256）。
  - `run.selfcheck` FAIL → 报告里紧跟着的 “direct FUSE run” 几行会写原因；常见是 FUSE 问题，
    脚本已自动改用 `--appimage-extract-and-run`（不需要装 fuse）；如果两种都不行，把报告发回。
  - `loader.packaged` / `loader.packaged-libc` FAIL → 包内运行时没被用上，把报告发回（这是机制问题）。
  - `lint.real` FAIL → 包内 linter 没给出 line-length 诊断，把报告发回。
  - `launch.project` FAIL → 程序没能启动/没正常退出，把报告发回。
  - `kernel.is-3.10` FAIL 且 `actual=7.0.0-…` 之类的宿主内核 → 你是在**容器**里跑的；
    容器与宿主共享内核，这条只有在 VM 里才有意义（见 §5）。
- 每条 `RESULT` 都带 `expected=` 与 `actual=`，评审只看这两栏；不要用“看起来没问题”代替。

---

## 5. 重要：容器 ≠ 真机

`kernel.is-3.10` 这条检查专门用来区分两者：**容器里永远是宿主内核**，所以在容器里跑这条必然 FAIL。
我们在 `stock centos:7` 容器里做过脚本自测，结果是
`SUMMARY: PASS=15 FAIL=1 SKIP=0`，唯一 FAIL 就是 `kernel.is-3.10`（预期，自测日志见
`evidence/centos7-final-verify/03-selftest-final.log`）。**不要把容器结果当成真机结果发回。**

---

## 6. 有屏幕的话：手工看一眼（可选，不计入脚本判定）

报告末尾有一个“manual checks”栏。如果这台 VM 有桌面：

1. 文件管理器里**双击** `VEdit-0.2.2-x86_64.AppImage`（或 `./VEdit-0.2.2-x86_64.AppImage`）；
2. 窗口能否打开；
3. 打开一个 `.v`/`.sv` 文件，看关键字是否有颜色；
4. 改一行，`Ctrl+S` 保存，再打开确认内容变了；
5. 看底部“诊断”面板有没有给出真实 lint 信息。

在报告的手工栏里把四项各写 `通过` / `不通过`（可以附一句现象）。**这一栏不参与 SUMMARY**，
但对我们判断“真实桌面体验”很有用。

---

## 7. 常见问题

**Q：需要装 libfuse2 吗？**
不需要。脚本先试直接运行（FUSE），失败会自动改用 `--appimage-extract-and-run`（或
`APPIMAGE_EXTRACT_AND_RUN=1`），两扇门都试，报告会写明“用了哪条”。

**Q：会改我的系统吗？**
不会。不写系统目录、不装包、不联网；只在自己的临时目录（`/tmp/vedit-verify.*`）里解包，
跑完自动清理（设 `VEDIT_KEEP_TMP=1` 可保留以便排查）。唯一会动的是：如果 AppImage 没有执行位，
脚本会 `chmod +x` 它。

**Q：能指定别的文件名/路径吗？**
可以：`bash verify-appimage-centos7.sh /path/to/Some.AppImage`。不加参数时脚本会找同目录下的
`VEdit-0.2.2-x86_64.AppImage`（或 `VEdit-*x86_64.AppImage`）。

**Q：报告太大发不出去？**
报告是纯文本（约 20–40 KB），正常邮件/聊天都能发；里面主要是程序自检的完整输出。

**Q：脚本自己会不会“假装通过”？**
不会：每条检查都有真实的 `expected`/`actual` 证据；做不了的检查会写 `SKIP` 并注明原因，
不会默认 PASS。`RESULT` 行就是结论。
