#!/usr/bin/env python3
"""Apply the batch-2 zh_CN translations to translations/vedit_zh_CN.ts.

Order-sensitive: the 182 entries below are the <translation type="unfinished">
messages of the catalogue in FILE ORDER, which is the order lupdate emitted them
(see 02-unfinished-before.txt). A handful of indices are cross-checked against
their expected English source before anything is written.
"""
import re
import sys

TS = "translations/vedit_zh_CN.ts"

TR = [
    # --- editor: Ctrl+click / go-to-definition (1) ---
    "此处无法转到定义：%1——Ctrl+click 现在与普通单击相同",
    # --- module tree / module catalog (2-16) ---
    "未打开工程——请打开文件夹以构建模块目录。",
    "模块目录：工程模型就绪后重试…",
    "空状态：尚无模块目录。请打开工程并按“刷新”。",
    "接口",
    "空状态：模块目录就绪后，焦点模块的端口将显示在此处。",
    "此构建不含 Rust 服务层（vd-ffi），因此模块目录无法运行",
    "无工程根目录：请打开文件夹，或以 --project <dir> 启动 vedit-app",
    "工程展开被拒绝：%1",
    "此工程没有源文件，因此没有可构建的模块目录",
    "模块目录被拒绝：%1",
    "正在编目 %1 个工程文件…",
    "模块目录服务在 30 秒内没有响应",
    "工程展开失败：%1",
    "模块目录失败：%1",
    "未知错误",
    # --- diagnostics: hover / fix / rename / project lint (17-29) ---
    "悬停 · %1",
    "正在检查引擎能否修复…",
    "无法应用修复：文档拒绝了此次编辑（是否为只读？）",
    "已用 verible-verilog-lint 修复 %1（一次撤销即可还原）",
    "无法使用重命名：%1",
    "重命名需要光标位于符号上（第 %1 行第 %2 列没有符号）",
    "此处无法重命名：%1",
    "重命名 %1",
    "%1 拒绝了重命名请求",
    "重命名被拒绝：%1",
    "工程文件集仍在加载——请稍后再试",
    "工程 lint 失败：%1",
    "自动工程 lint 未启动：%1",
    # --- interface panel (30-51) ---
    "双击端口：跳转到其声明。Ctrl+双击：跳转到本模块的例化语句。模块目录没有逐连接的位置信息，因此 Ctrl+双击定位到例化语句，而不是被连接的端口。",
    "名称",
    "方向",
    "类型",
    "位宽",
    "双击端口查看其声明；Ctrl+双击查看例化语句。",
    "尚无模块目录：请打开工程并按“刷新”。",
    "未找到模块：slang 未报告此工程的任何模块定义。",
    "没有焦点模块——请在模块树中选择一个模块，或将光标置于模块定义内。",
    "模块 %1 · %2 个端口",
    "位宽由 `%1` 推导",
    "%1 · 双击：跳转到端口声明；Ctrl+双击：跳转到本模块的例化语句",
    "端口 %1 在模块目录中没有声明位置，因此未打开任何内容",
    "端口 %1 的声明",
    "未编目到 %1 的任何例化：它是设计顶层，因此没有可跳转的实例语句",
    "%1 · %2 · %3",
    "已取消例化选择",
    "%1 中的例化在模块目录中没有源位置",
    "%1 在 %2 中的例化",
    "%1：未注册跳转目标（未安装编辑器 lane），因此未打开任何内容",
    "%1：编辑器拒绝了跳转到 %2 的请求",
    "%1：跳转请求（%2）被拒绝",
    # --- autofix (52-60) ---
    "未找到 verible-verilog-lint：无法自动修复",
    "无法读回修复后的副本",
    "lint 工具在自动修复期间崩溃",
    "此诊断没有规则 id，因此无法选择自动修复",
    "没有可用的临时目录",
    "无法写入临时副本",
    "无法启动 lint 工具：%1",
    "自动修复运行超时",
    "无法启动 lint 工具",
    # --- diagnostic hover severity (61-73) ---
    "错误",
    "警告",
    "信息",
    "提示",
    "诊断",
    "此后端不为此诊断提供自动修复",
    "正在检查自动修复…",
    "无自动修复：%1",
    "修复",
    "<a href=\"jump\">⚠ %1 个工程错误——点击跳转</a>",
    "自动工程 lint 发现 %1 个错误。每次点击跳转到下一个。",
    "工程 lint 错误",
    "工程 lint 报告了 %1 个错误。双击其中一条即可跳转。",
    # --- module catalog summary / type widths (74-86) ---
    "模块目录：%1",
    "尚无模块目录",
    "%1 · %2 个模块 · %3 个顶层 · %4 ms · %5 KiB 峰值",
    " · ⚠ %1",
    "ABI 未报告此端口的类型字符串",
    "类型 `%1` 存在未闭合的打包维度",
    "`%2` 中的打包范围 `[%1]` 不是字面量 msb:lsb 对，因此无法推导位宽",
    "`%2` 中的打包范围 `[%1]` 过大，无法表示",
    "`%1` 中的打包维度超出支持的位宽",
    "已达到深度上限 %1——此模块的子模块未展开",
    "模块树在 %1 行处被截断",
    "循环：%1 已是本行的祖先——未展开",
    "未为 `%1` 编目到模块定义",
    # --- module tree rows / panel (87-102) ---
    "循环：%1",
    "已达深度上限（%1）",
    "树已截断（%1 行）",
    "为整个工程重新运行模块目录",
    "过滤模块…",
    "模块",
    "例化次数",
    "定义位置",
    "无可刷新内容：没有模块目录客户端",
    "无顶层模块：所有已编目的模块都被其他模块例化，因此层次结构没有根。",
    "\n根目录：%1",
    "模块树就绪——双击模块即可打开其声明。",
    "双击：打开此模块的声明",
    "此标记行没有位置",
    "%1 在模块目录中没有定义位置，因此未打开任何内容",
    "%1 的定义",
    # --- project replace core (103-110) ---
    "替换不可用：此构建未链接原子保存路径",
    "替换被拒绝：该操作没有查询，无法校验范围；未写入任何内容",
    "替换被拒绝：命中列表没有冻结的扫描标识；未写入任何内容",
    "替换：%1：第 %2 行属于另一次搜索；该文件未写入任何内容（请重新搜索）",
    "替换：%1：第 %2 行不在冻结的扫描快照范围内；该文件未写入任何内容（请重新搜索）",
    "替换：%1：第 %2 行：扫描运行期间文档被写入，命中位置可能已失效；该文件未写入任何内容（请重新搜索）",
    "替换：%1：第 %2 行：扫描之后文档发生变化（大小或修改时间）；该文件未写入任何内容（请重新搜索）",
    "替换：%1：第 %2 行：读取期间文档发生变化；该文件未写入任何内容（请重新搜索）",
    # --- find/replace bar (111-118) ---
    "当前文件",
    "整个工程",
    "搜索本文档，或将查询交给搜索面板",
    "整个工程的替换在搜索面板中执行",
    "整个工程 → 搜索面板",
    "输入查询以搜索整个工程",
    "整个工程：正在搜索面板中搜索",
    "整个工程：打开搜索面板以搜索整个工程",
    # --- search panel controls (119-130) ---
    "全词匹配",
    "正则表达式（全工程：RE2 风格，不支持反向引用和环视）",
    "当前文件",
    "整个工程",
    "搜索当前文档或整个工程",
    "替换为",
    "替换",
    "替换当前匹配并前进到下一个",
    "全部替换",
    "替换所有匹配（先以文件和命中数请求确认）",
    "替换仍在进行中，因此未应用 Ctrl+F 交接。请等待其完成后再重新搜索。",
    "替换仍在进行中，因此未启动搜索。请等待其完成后再运行搜索。",
    # --- replace-all preflight (131-145) ---
    "正在统计匹配数…",
    "无当前文件。请打开文档或选择“整个工程”。",
    "全部替换被拒绝：统计扫描运行期间查询或选项发生变化；未替换任何内容。请重新搜索后再执行全部替换。",
    "统计扫描已取消",
    "全部替换被拒绝：%1",
    "全部替换被拒绝：已达到结果上限（%1）；请先缩小查询范围",
    "全部替换被拒绝：统计扫描在 %1 ms 时间预算处停止，命中列表不完整；请先缩小查询范围",
    "全部替换被拒绝：“%1”在统计扫描运行期间被写入，命中位置可能已失效；未替换任何内容。请重新搜索后再执行全部替换。",
    "搜索运行期间查询或选项发生变化；请重新搜索。",
    "查询或选项已变化；请重新搜索。",
    "结果列表没有冻结的扫描标识；替换前请重新搜索",
    "该操作没有查询，无法校验范围；未经验证拒绝写入",
    "查询为空，无法校验范围；替换前请重新搜索",
    "自扫描以来查询或匹配选项已变化；替换前请重新搜索",
    "自扫描以来搜索范围或选项已变化；替换前请重新搜索",
    # --- replace refusals (146-168) ---
    " 未替换任何内容。",
    " 已保存的 %1 个文档仍保留在磁盘上；未替换任何内容。",
    "编辑器桥接不可用，无法检查未保存的缓冲区状态",
    "“%1”：%2",
    "全部替换被拒绝：编辑器桥接不可用，无法检查未保存缓冲区状态。",
    "全部替换被拒绝：有未保存的编辑器文档无法保存：%1",
    "已保存的 %1 个文档仍保留在磁盘上；未替换任何内容。",
    "未替换任何内容。",
    "没有可替换的匹配；请先运行搜索。",
    "替换被拒绝：%1",
    "替换被拒绝：此行属于较早的搜索；替换前请重新搜索。",
    "结果列表已过期；请重新搜索。",
    "替换被拒绝：此匹配没有报告可替换范围。",
    "替换被拒绝：“%1”不在冻结的扫描快照范围内；未写入任何内容（请重新搜索）。",
    "替换被拒绝：“%1”在搜索运行期间被写入，此匹配可能已移动；未写入任何内容。请重新搜索。",
    "替换被拒绝：“%1”在搜索之后发生变化；未写入任何内容。请重新搜索。",
    "替换被拒绝：“%1”中未保存的编辑移动了第 %2 行，此匹配已不在搜索找到的位置；该文档未保存，也未替换任何内容。请保存后重新搜索。",
    "替换被拒绝：“%1”有无法保存的未保存更改：%2",
    "全部替换：没有可替换的匹配。",
    "全部替换已取消；",
    "未写入任何内容。",
    "确认对话框打开期间查询或选项发生变化",
    "全部替换被拒绝：%1；未替换任何内容。请重新搜索后再执行全部替换。",
    # --- confirmation / execution report (169-182) ---
    "将“%1”替换为“%2”？\n\n将替换 %4 个文件中的 %3 处。",
    "\n\n已先保存 %1 个受影响的未保存编辑器文档：%2",
    "替换被拒绝：该操作没有查询，无法校验范围；未写入任何内容。",
    "替换被拒绝：%1；未写入任何内容。请重新搜索。",
    "替换被拒绝：有命中不属于正在替换的扫描；未写入任何内容。",
    "替换不可用：此构建未链接原子保存路径。",
    "正在替换…",
    "正在替换 %1 处…",
    "已替换 %2 个文件中的 %1 处。",
    "已跳过 %1 个文件（非有效 UTF-8 或无法读取）。",
    "替换已取消；已写入 %2 个文件中的 %1 处。",
    "在写入 %2 个文件中的 %1 处之后替换失败：%3",
    "替换失败：%1",
    "%1 个文件在替换期间发生变化——未重新加载：%2",
]

# index -> expected fragment of the English source (guards against off-by-one)
ANCHORS = {
    1: "Go to Definition is unavailable here",
    5: "Interface",
    17: "Hover",
    30: "Double-click a port: jump to its declaration",
    45: "%1 · %2 · %3",
    69: "修复",
    70: "<a href=\"jump\">",
    93: "Instantiations",
    94: "Definition",
    111: "Current File",
    120: "RE2",
    146: " Nothing was replaced.",
    165: "Replace All cancelled;",
    169: "with",
    182: "changed during replace",
}


def escape(text: str) -> str:
    return (
        text.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
        .replace("'", "&apos;")
    )


def main() -> int:
    text = open(TS, encoding="utf-8").read()
    pat = re.compile(
        r'(<message>\n(?:\s*<location[^>]*/>\n)*\s*<source>)(.*?)(</source>\n\s*'
        r'<translation type="unfinished"></translation>)',
        re.S,
    )
    matches = list(pat.finditer(text))
    if len(matches) != len(TR):
        print(f"REFUSED: {len(matches)} unfinished messages, {len(TR)} translations")
        return 2

    problems = []
    for i, m in enumerate(matches, 1):
        src = m.group(2)
        if i in ANCHORS and ANCHORS[i] not in src:
            problems.append(f"anchor {i}: expected {ANCHORS[i]!r} in {src!r}")
        if not TR[i - 1].strip():
            problems.append(f"empty translation at {i} ({src!r})")
    if problems:
        print("REFUSED: anchor/emptiness problems")
        for p in problems:
            print("  ", p)
        return 3

    out = []
    pos = 0
    for i, m in enumerate(matches):
        out.append(text[pos : m.start()])
        out.append(m.group(1))
        out.append(m.group(2))
        out.append(m.group(3).replace(
            '<translation type="unfinished"></translation>',
            "<translation>" + escape(TR[i]) + "</translation>",
        ))
        pos = m.end()
    out.append(text[pos:])
    new = "".join(out)

    if new.count('<translation type="unfinished"') != 0:
        print("REFUSED: unfinished left over")
        return 4
    open(TS, "w", encoding="utf-8").write(new)
    print(f"applied {len(matches)} translations")
    return 0


if __name__ == "__main__":
    sys.exit(main())
