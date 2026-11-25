# 编辑器语言验收

`vedit-editor-language-tests` 通过产品的 `EditorArea` 打开文件并检查 KTextEditor 的实际样式。filelist 场景通过键盘接受目录和文件，验证普通路径、`-f`、`-F`、`-I`、`+incdir+` 和撤销。Markdown 在 `.md`、`.markdown` 以及明暗两种主题下检查。Verilog 和 SystemVerilog 的断言覆盖定义名、例化名、`if/else`、`begin/end`、括号、注释和字符串。

在带精简语法库的 AppDir 中重复运行渲染检查。点击 SystemVerilog 编辑器的折叠标记，隐藏行数依次为 0、38、0。[渲染记录](render.txt) · [本批验收](../README.md)
