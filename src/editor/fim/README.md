# 编辑器侧 FIM lane —— Slice 3，任务 T27（AC-S3.2 残余）

FIM（fill-in-the-middle）的**编辑器侧 inline completion 客户端**。本 lane 只做三件事，
绝不自己发 HTTP：

1. 光标处切分 prefix/suffix（`contextAt`）；
2. 经 **Rust 服务层**（C ABI `vd_fim_complete` / `vd_fim_poll`，`crates/vd-ffi/src/fim.rs`
   → `crates/vd-fim` → ureq）请求补全；
3. 维护建议状态机：`onTextEdited` 自动作废、`accept()` 取一次、`cancel()`、短状态文案。

默认关闭时 Rust 侧不建 transport、不建 worker、不开 socket；本 lane 只显示
「FIM 未启用（默认关闭）」，不产生建议、不阻塞编辑。

## 文件

| 文件 | 职责 |
|---|---|
| `fim_backend.h/.cpp` | 接缝：`Backend::complete(prefix, suffix)`；生产实现 `makeRustBackend()` 把调用转进 C ABI（`VEDIT_FIM_HAVE_FFI`）；`contextAt` 切分光标前后文本 |
| `fim_inline_completion.h/.cpp` | 无头状态机：request / 建议 / 输入作废 / accept 一次 / cancel / 状态文案 |
| `fim_selftest.cpp` | 无头自测：假 backend 状态机 + `--default-off-only`；有 Rust 链时再用**本地假端点**证明「未配置 0 连接」「配置后 1 次请求由 Rust 发出」 |
| `CMakeLists.txt` | 独立 lane 构建；`TARGET vd_ffi` 或 `-DVEDIT_FFI_STATICLIB=… -DVEDIT_FFI_INCLUDE_DIR=…` 时接 Rust 服务，否则显式 SKIP |

## 未接线（如实）

本 lane **尚未接进 `src/editor/kernel`**。要真正在编辑器里出现 ghost text，需要在
`src/editor/kernel/**`（T94/T81 的写域）加一个 completion hook：在
`EditCoreProvider::createEditor()` 处为每个 `KTextEditor::View` 建一个
`fim::InlineCompletion`，把视图的 `cursorPositionChanged`/`textChanged` 映射到
`onTextEdited`，把触发命令（`Alt+\`）映射到 `request(document->text(), cursor.line(), cursor.column())`，
并把 `accept()` 的结果写入文档。该接线与 FIM 面板/命令注册属于后续任务。
