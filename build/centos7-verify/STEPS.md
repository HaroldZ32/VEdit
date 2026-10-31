# 在真 CentOS 7 上验证 VEdit（照着做即可）

这个包把 VEdit 需要的**所有东西**都带在身上：Qt6/KF6 库、glibc 与它自己的 loader、
Qt 平台插件、随包工具链（verible / slang），以及一个样例 Verilog 工程。
**目标机不需要装 Qt6/KF6，也不需要联网**；只需要一台能跑 shell 的 CentOS 7。

> 为什么连 glibc 一起带：VEdit 的构建基线是 glibc 2.43，CentOS 7 只有 glibc 2.17。
> 只拷 Qt6/KF6 的 .so 是不够的（会在启动时报 `GLIBC_2.43 not found`），
> 所以包里带了 glibc + 自带的 `ld-linux-x86-64.so.2`，启动时用包里的 loader。

---

## 1. 把 tarball 拷到虚拟机

在能访问虚拟机的机器上（把 `<你的路径>` 换成实际路径）：

```sh
scp vedit-centos7-verify-<sha>.tar.gz <用户>@<虚拟机IP>:/tmp/
```

（U 盘 / 共享目录 / `docker cp` 也可以，怎么方便怎么来。）

## 2. 在虚拟机上解压并跑一条命令

```sh
cd /tmp
tar xzf vedit-centos7-verify-<sha>.tar.gz
cd vedit-centos7-verify-<sha>
bash verify-centos7.sh
```

脚本会逐项自检、**每项打印一行 `RESULT PASS|FAIL|SKIP`**，跑完在**当前目录**留下
`verify-report-<主机名>-<时间>.txt` —— **把这个文件整份发回来即可**。

> 不需要 root，不需要联网，不需要显示器（用 offscreen 无头模式）。
> 如果脚本提示缺 `readelf`/`objdump`，先 `yum install -y binutils` 再重跑。

## 3. 报告怎么看（只看这 8 行）

| 检查项 | 期望 | 说明 |
|---|---|---|
| `kernel.is-3.10` | **PASS，actual 是 `3.10.*`** | **最关键**：证明真的是 CentOS 7 内核。若是别的内核（容器/共享宿主内核），这条会 FAIL，说明这次结果不能代表 CentOS 7 |
| `glibc.system` | 任意（只是记录） | 记下系统 glibc（CentOS 7 应为 2.17） |
| `package.files` / `package.manifest` | PASS | 包是否完整、sha256 是否一致（防传输损坏） |
| `app.selfcheck` | PASS（exit 0） | 应用能启动并自检通过 |
| `startup.toolchain` | PASS，含 `source=verible:bundled` | 应用**找到并用上了包里的工具链**（不是系统 PATH 的） |
| `highlight.stack` | PASS，`grammar=verilog backend=1 startup-hook=1` | 高亮栈真的启动了 |
| `highlight.verilog-definition` | PASS，`verilog=1` 且 `own-xml` 非空 | **.v 是否真的会高亮**。若这条 FAIL（当前构建在这台容器里就是 FAIL，`verilog=0`），说明产品自己判定“Verilog 定义不可用 → .v 退化为纯文本”；这是**包形态的问题**，报告里会原样给出 `syntax-product:` 行 |
| `lint.bundled` | PASS，诊断数 > 0 | 随包 verible lint 在样例预置违规上真的报出诊断 |
| `symbols.app` | PASS + 打印实际最高 GLIBC | `vedit-app` 需要 2.39（Linux 构建基线），由**包内 glibc 2.43** 提供；这条通过说明包能自洽 |
| `loader.used` | PASS，`/proc/<pid>/maps` 里有包内 loader | **机制级证据**：进程真的用包里的 loader + glibc 跑，而不是系统那个 |

`SKIP` 不是失败：脚本会写明原因（例如本包没带某个可选探针）。

## 4. 失败了怎么办

1. 报告里 **`RESULT FAIL` 的那几行**就是问题所在，`note=` 里有原因。
2. 第一次失败时，把对应的日志文件也发回来（报告里会写路径，例如
   `/tmp/vedit-startup.XXXXXX`）。
3. 最常见的两种：
   - `kernel.is-3.10` FAIL：你实际不在 CentOS 7 内核上（例如在容器里跑的，
     容器共享宿主机内核）。请在真正的 CentOS 7 虚拟机里跑。
   - `loader.used` FAIL / `app.selfcheck` 失败：先确认 `ls AppDir/ld-linux-x86-64.so.2`
     和 `ls AppDir/lib/libc.so.6` 都在（`package.files` 那行也会报）。

## 5. 手动复现（脚本内部就是这么做的）

```sh
cd vedit-centos7-verify-<sha>
export QT_PLUGIN_PATH=$PWD/AppDir/plugins
export QT_QPA_PLATFORM=offscreen
export XDG_DATA_DIRS=$PWD/AppDir/usr/share XDG_DATA_HOME=$PWD/AppDir/usr/share
export VEDIT_SYNTAX_DIR=$PWD/AppDir/usr/share/org.kde.syntax-highlighting
export LANG=C.UTF-8
./AppDir/ld-linux-x86-64.so.2 --library-path ./AppDir/lib ./AppDir/bin/vedit-app \
    --project ./samples/verilog-demo --hold 1500
# 想开真窗口（无 X 服务器时用 offscreen；有 DISPLAY 时把上面 QT_QPA_PLATFORM 去掉或设为 xcb）
```

## 6. 这个包不验证什么（照实说）

- 它的 AppDir 是在**Ubuntu 26.04（glibc 2.43）构建基线上的产物**；真机上跑通只证明
  “打包 glibc + 自带 loader”这条路在你的机器上成立，**不等于**从源码在 CentOS 7 上
  原生构建的产物（那是另一条路线，见 `evidence/glibc217-feasibility/`）。
- `highlight.mode`（KTextEditor 的 `highlightingMode`）需要带窗会话才能观测，本包用
  offscreen 无头验证，脚本会把这一项标成 SKIP 并写明原因。
- 未覆盖：输入法、字体渲染质量、真实 X11 窗口交互、多显示器。

---

### 附：tarball 里有什么

```
vedit-centos7-verify-<sha>/
  AppDir/
    bin/vedit-app                 # 产品二进制
    bin/verible-verilog-{ls,lint,format}, bin/slang   # 随包工具链
    bin/vedit-ksh-env-probe, bin/vedit-m0-smoke        # 运行期守卫/冒烟
    lib/                          # Qt6/KF6/glibc 等全部依赖（含 libc.so.6）
    ld-linux-x86-64.so.2          # 自带 loader
    plugins/platforms/            # Qt 平台插件（offscreen/xcb）
    usr/share/                    # 语法定义白名单、工具链清单、字体配置、licence
  samples/verilog-demo/           # .f + 带故意违规的 bad.v（lint 会报 3 条）
  verify-centos7.sh               # 一条命令跑完的验证脚本
  STEPS.md                        # 本文件
  MANIFEST.sha256                 # 每个文件的 sha256
```
