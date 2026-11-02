# T114 — 重打包影响（给 captain / Lead）

**结论：需要重新构建 + 重新打包，现有 AppImage（`e7ae067f…`）不包含此修复。**

链路（全部静态链接进产品二进制）：

```
crates/vd-config/src/{project.rs,warnings.rs}   ← T114 的修复
        ↓ (静态 Rust 服务层)
crates/vd-ffi/src/project.rs                    ← vd_project_parse，纵深防御过滤
        ↓ (libvd_ffi.a)
vedit-app（Qt6 产品）+ AppDir/AppImage 里的 bin/vd-app
        ↓
用户 GUI 的 "Diagnostics (N)" 列表
```

- 影响面：任何调用 `vd_project_parse` 的消费方（项目面板、诊断面板的文件集获取）都受影响；
  这是**产品二进制**的改动，不是纯配置或纯文档。
- 因此：用户实机上的那条 `rules.search_stopped: /home/haroldzhao: …` 只有在新构建 + 新 AppImage
  之后才会消失；旧包保持现状。
- 按 captain 要求：**T114 不触发重打包**。等实机另外四项观测结果一起确认后，由 Lead 一次性提交并重打。
- sha256 / 新包名由打包任务（T31）负责，本任务不打包、不 commit/push。
