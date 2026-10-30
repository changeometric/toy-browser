> 本文为中文翻译，英文原版为准：[roadmap.md](../roadmap.md)。

# 路线图

每个里程碑都是一个最小垂直切片：小、可构建、可演示、可测试。一个里程碑在其演示命令可用、新逻辑有测试、文档仍然准确时才算完成。

**当前状态：M0–M5 已完成；下一步是 M6（绘制）。** 快照与接管指南见 [handoff.md](handoff.md)。

## M0 — 设计与骨架 ✅（已完成）

- 文档、可构建的 CMake 骨架、桩可执行文件、工具链冒烟测试。
- 当 `cmake --build` 和 `ctest` 通过且用户接受文档时完成。

## M1 — 窗口、事件循环、第一批像素 ✅（已完成）

- `render`：`Surface`、实心矩形填充、PPM writer。
- `platform`：X11 窗口后端（打开/关闭、resize、expose、按键事件、呈现 ARGB 表面），无显示器时优雅失败。
- `shell` + CLI：占位页、窗口事件循环、`--headless --screenshot`、`--size`。
- 已达成验收：窗口显示占位页，同一帧可以在无显示器下 headless 导出，并且 `render.surface`、`render.ppm`、`platform.window_requires_display`、`shell.headless_screenshot` 在 GCC 和 Clang 下、警告视为错误时全部通过。

## M2 — 抓取 URL ✅（已完成）

- 对绝对 `http` 和 `file` URL 的 URL 解析：百分号解码、默认端口，尚无 base 解析。
- `net` 模块：基于 POSIX socket 的阻塞 HTTP/1.1 GET，每个连接一个请求，连接与读取超时，`Content-Length` 与 chunked body，重定向（301/302/303/307/308），16 MiB body 上限；`file://` 读取本地文件。
- CLI：`--dump-body <url>` 把原始 body 写到 stdout。全部 headless；不需要显示器。
- 已达成验收：对 `file://` 和对一个 loopback HTTP 服务器得到逐字节相同的 dump；错误路径以非零退出；`net.url`、`net.http`、`net.file`、`shell.dump_body_file` 和 `shell.dump_body_missing_file` 在 GCC 和 Clang 下、警告视为错误时通过。无 TLS。
- M2 已知限制：非 2xx 响应仍然算成功的抓取（状态在 API 中暴露；错误页与导航以后再说），并且 shell 层端到端测试覆盖 `file://`，而 HTTP 由进程内服务器测试覆盖。

## M3 — HTML 到 DOM ✅（已完成）

- `dom` 模块：自写 tokenizer 与构建在 arena DOM 之上的基于栈的树构建器，带错误容忍（畸形输入永不失败）、确定性 dump 格式，以及约 40 个 golden dump 用例加实体测试。
- `--dump-dom <url>`：通过 `net` 抓取、解析、把 DOM dump 到 stdout；完全 headless。
- 已达成验收：`dom.parser`、`dom.entities`、`shell.dump_dom_file` 和 `shell.dump_dom_missing_file` 在 GCC 和 Clang 下、警告视为错误时通过；一个本地 HTTP 页面完成 抓取 → 解析 → dump 的往返。
- M3 已知限制（刻意的最小 MVP 子集）：没有 HTML5 插入模式（除 EOF 外没有隐含结束标签、没有 foster parenting、没有 adoption agency），doctype 只能出现在 `<html>` 之前，没有隐式 head 内容路由（没有显式 `<head>` 的 `<title>` 会落到 body 中），注释逐字保留（因此源码注释中的空格会出现在 dump 里），空白文本节点以简单路由保留，实体是一小组命名实体加数字形式（要求分号，仅小写 `x`），没有 `<template>`、SVG 或 MathML 命名空间，且输入假定为 UTF-8。

## M4 — CSS 与计算样式 ✅（已完成）

- `css` 模块：自写的规则、声明和简单选择器 tokenizer/解析器（类型、class、id、通配、后代/子代组合器、逗号分组），跳过 at-rule 与不支持的语法，能容忍畸形输入。
- `style` 模块：由我们自己的 CSS 解析器解析内置 UA 样式表、选择器匹配、按 specificity（优先级）与 `!important` 做 UA/作者/行内层叠、继承，以及支持的属性集（display、color、background-color、font-size 的 px/em、font-weight、font-style、margin/padding 长写属性与 1-4 值简写、width/height 的 auto 或 px/em）。
- `--dump-style <url>`：把逐元素的确定性计算样式树输出到 stdout，完全 headless。
- 已达成验收：`css.parser`、`style.cascade`、`style.dump`、`shell.dump_style_file` 和 `shell.dump_style_missing_file` 在 GCC 和 Clang 下、警告视为错误时通过（共 20 个测试）。
- M4 已知限制：没有外部 `<link>` 样式表（需要 URL 解析与抓取；以后再做），没有 at-rule 内容，没有伪类/伪元素，没有属性选择器，没有 `rgb()`/`hsl()`/`calc()`、`inherit`/`initial` 关键字或 CSS 变量，没有 `%`/`pt`/`rem` 单位，没有 `margin: auto`，命名颜色使用标准值（例如 green = #008000）。

## M5 — 布局 ✅（已完成）

- `layout` 模块：arena 形式的 `BoxTree`，带绝对 CSS 像素几何（border-box 矩形；几何用 `float`，dump 中取整），块级与行内盒、匿名盒、块宽度/margin/padding、auto 与指定高度、使用调用方提供的文本度量回调进行换行、行内元素分段、`<br>`，以及确定性的盒 dump（缩进上限 64 层）。
- `render`：内置等宽位图字体度量（`toy/render/font.h`），8x16 设计网格、16 px em。M5 只交付度量；字形绘制在 M6 到来。
- `shell`/CLI：`--dump-boxes <url>` 完成抓取、解析、样式、布局并 dump；视口宽度取 `--size` 的宽度（默认 800）。完全 headless。
- 已达成验收：`render.font_metrics`、`layout.block`、`layout.inline`、`layout.line_break`、`layout.dump`、`layout.deep_nesting`、`shell.dump_boxes_file`、`shell.dump_boxes_size`、`shell.dump_boxes_missing_file`、`shell.dump_boxes_missing_url` 和 `shell.dump_boxes_conflict` 在 GCC 和 Clang 下、警告视为错误时通过（共 31 个测试，确定性且 headless）。
- M5 已知限制（MVP 简化）：没有 margin 合并（父元素的 auto 高度包含其流内子元素的 margin）；没有基线对齐（同一行的盒顶部对齐并共享行高）；只应用行内元素的水平 margin/padding（行内垂直 margin/padding 以及行内 width/height 被忽略）；不支持 `text-align`/`line-height`/`vertical-align`/`white-space`，因此 `<pre>` 也和其他元素一样折叠空白；只在 ASCII 空白处换行（非 ASCII 文本整体视为一个单词：没有 Unicode 换行、断字或强制断行）；没有百分比单位；并且布局暂时不绘制任何内容。

## M6 — 绘制：第一个真实页面（下一轮）

- 从盒树生成显示列表、矩形与文本的光栅化器、裁剪；接入窗口。
- 当一个带标题、段落和链接的本地 HTML 页面在窗口中渲染出来，并有 golden 截图测试时完成。

## M7 — 导航

- 可点击链接、滚动、前进/后退历史、窗口标题。
- 当一个小的本地多页站点可以浏览时完成。

## M8 — 真实字体与图像（需要审批）

- TTF 文本与 `<img>` 解码，使用已批准的库或我们自己的实现。
- 当一个带真实字体文本和至少一张图片的页面渲染出来时完成。

## M9 — JavaScript 子集（可选，工作量大）

- 面向类似 ES5 的极小子集的词法分析器、解析器、小型解释器、DOM 绑定和事件处理器。
- 当 fixture 页面的脚本修改 DOM 且修改被绘制出来时完成。

## M10+ — 机会性工作

更多 CSS（flex、表格）、表单、更好的文本处理、性能、一致性测试套件。每一项都只在前一个切片干净、且用户同意优先级之后才开始。

## Backlog — 计划中的后续工作

- **代理支持（M2.1）**——通过 HTTP 代理抓取 `http://` URL：`FetchOptions` 中的代理设置、`--proxy host:port` CLI 标志与 `http_proxy` 环境变量回退；发往代理的请求使用 absolute-form 请求目标。测试使用进程内的假代理。用户于 2026-10-31 推迟：只有当真实站点测试需要它，或它成本低且明显有用时才实现。
- **TLS / `https://`**——用户于 2026-10-31 推迟；OpenSSL 未获批准。访问 Google 之类的站点需要 `https`，仅靠代理支持不够。
- **测试辅助代码合并**——较旧的测试可执行文件里仍然重复着每个文件各自的 `Check`/`g_failures` 测试支架。header-only 的 `tests/support/check.h`（接口目标 `toy_test_support`）已在 M5 引入，新测试都在使用；旧文件只在被改动时迁移。不引入框架依赖。

## 规则

- 前一个里程碑的演示可用之前，不要开始下一个里程碑。
- 绝不让里程碑因等待依赖审批而阻塞：先交付 headless 或自实现切片，审批通过后再升级。
- 任何里程碑都可以被用户重新划定范围。MVP 永远优先于完整性。
- headless 优先：任何可见或可观察的结果都必须能在不开窗口的情况下检查（dump 命令或截图），这样项目可以完全 headless 地开发和测试。
