# 在 CentOS 7 上验证 VEdit 发布包

在 CentOS 7.9 x86_64 物理机或运行 3.10 内核的虚拟机上执行以下步骤。发布流水线的 CentOS 7 容器检查覆盖 glibc 2.17 用户空间，容器使用 runner 的内核。

## 核对下载和环境

把三个发布包及同版本 `SHA256SUMS` 放在同一目录。确认该目录和临时目录有足够空间，且挂载点允许执行程序。

```bash
uname -m
uname -r
getconf GNU_LIBC_VERSION
sha256sum --check --strict SHA256SUMS
```

报告应记录 `x86_64`、`3.10` 系列内核、`glibc 2.17`，以及三个文件的 `OK`。如果只验证一个格式，按[用户手册](../../../docs/USER_GUIDE.md#附录-a-产物形态与安装)从清单校验对应文件，并记录实际验证的包。

## 启动三种格式与运行自检

以下用 `0.4.1` 作示例。`TMPDIR`（未设置时为 `/tmp`）需要允许临时解包后执行。用新的状态目录隔离验证会话。

```bash
chmod +x VEdit-0.4.1-x86_64.AppImage VEdit-0.4.1-x86_64.run
export QT_QPA_PLATFORM=offscreen VEDIT_NO_ENV_AUTOCAPTURE=1 VEDIT_STARTUP_TRACE=1
state_dir=$(mktemp -d)

./VEdit-0.4.1-x86_64.AppImage --appimage-extract-and-run --version
./VEdit-0.4.1-x86_64.AppImage --appimage-extract-and-run --state-dir "$state_dir" --selfcheck
tar -xzf vedit-0.4.1-linux-x86_64.tar.gz
./vedit-0.4.1-linux-x86_64/AppRun --version
./vedit-0.4.1-linux-x86_64/AppRun --state-dir "$state_dir" --no-session-restore --hold 500
./VEdit-0.4.1-x86_64.run --version
PATH= ./VEdit-0.4.1-x86_64.run --state-dir "$state_dir" --no-session-restore --hold 500
```

三个 `--version` 应输出 `vedit-app 0.4.1`。记录 AppImage 自检的结果、两次 `launch-ready` 以及各命令的退出码。最后一条命令应在 `VCS-TOOLCHAIN` 中报告 Git 和 SVN 缺失。安装 FUSE 的环境可以另外验证 AppImage 直接运行。如果有图形桌面，清除 `QT_QPA_PLATFORM` 再试打开、编辑与保存 HDL 文件，记录显示服务器类型。

## 单独生成 AppImage 报告

把本版本的 `verify-appimage-centos7.sh` 也复制到下载目录。运行以下命令，显式选择要检查的 AppImage：

```bash
bash verify-appimage-centos7.sh ./VEdit-0.4.1-x86_64.AppImage
```

脚本从 `SHA256SUMS` 读取对应的 AppImage 校验行，检查系统内核、包内 loader、自检、原生折叠和真实 HDL 工具。保存生成的 `verify-report-*.txt`。逐条查看 `RESULT` 及其原因，任何 `FAIL` 都需要解释，`SKIP` 表示该项未覆盖。

保存环境、下载文件大小和哈希、命令与输出。自动化包检查、F2 隔离和容器证据位置见[打包说明](../README.md)。
