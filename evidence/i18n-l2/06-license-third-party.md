# T38 — 许可与第三方归属（qtbase_zh_CN）+ `THIRD_PARTY_NOTICES.md` scope 例外记录

## 1. `THIRD_PARTY_NOTICES.md` 的写域例外（captain 授权）

- 该文件**不在合同原始写域**（原写域见 contract `i18n-l2.md` §Allowed write scopes）；
  captain 2026-11-02 邮件**书面授予**本条 scope 例外，要求：声明例外 + 记录前后 sha256。
- 追加位置：文件末尾新增 **§10「Qt6 产品线：Qt 标准对话框翻译（`qtbase_zh_CN.qm`，T38 / AC-9.3）」**，
  未改动 §1–§9 任何内容。
- sha256：

| 时点 | 字节数 | sha256 |
|---|---|---|
| 追加前 | 17,739 | `af69af60ee160574e06e387c736a947eade8e0f9ac962e9ad836b100125c6853` |
| 追加后 | 20,387 | `0e300e99f983c107d4a55d2536529ea4e4eadd574a19fd6f6d8afed7c12e9ea1` |

（`git diff THIRD_PARTY_NOTICES.md` 只有一段新增；`stat -c '%s'` 20,387 B。）

## 2. qtbase 的事实核对（以 Debian 包为准，不沿用假设）

- 包：`qt6-translations-l10n`（Qt 6.10.2+dfsg-7）。
- copyright 登记：`Files: * Copyright 2017-2021 The Qt Company Ltd.`，**License: GPL-3 with the
  Qt-1.0 exception**。
- **更正**：先前的 LGPL 假设不成立；**GFDL 只适用于 Qt 文档**，不适用于本 `.qm` 数据文件。
- 聚合方式：`.qm` 是独立的翻译数据文件（运行期 `QTranslator::load` 读取，不链接、不重编译），
  与 VEdit 的 MIT 代码不构成派生作品；分发时按上表登记来源/版本/许可/sha256。
- 文件身份：147,222 B，sha256 `0481a9b92a6f0578400e960a7d5ee70217a6a450eaf7f98b7d32b8dbd0d58616`，
  与 captain 记录逐字节一致。

## 3. 开放问题的结论（合同要求报告里给结论）

1. **可得性**：宿主可得（Debian 包安装路径），镜像内不可得（无目录、无 .qm、无 .ts 源）→
   **vendor 是唯一可复现路径**；本轮已 vendor 并四重验证（构建树/装载/打包/manifest）。
2. **许可归属**：GPL-3 + Qt-1.0 例外（非 LGPL、非 GFDL）→ 已在 `THIRD_PARTY_NOTICES.md` §10 按事实登记；
   全文/来源以 Debian 包 copyright 为准（仓库不复制 Qt 的 GPL 文本）。
3. **`THIRD_PARTY_NOTICES.md` 的最终措辞**由 captain 决定；本任务只提供事实与条目，不宣布许可结论之外的
   分发策略。
