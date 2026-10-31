> 本文为中文翻译，英文原版为准：[roadmap.md](../roadmap.md)。

# 路线图

每个里程碑都是一个最小垂直切片：小、可构建、可演示、可测试。一个里程碑在其演示命令可用、新逻辑有测试、文档仍然准确时才算完成。

**当前状态：M0–M8 已完成（M8a 图片、M8b 真实字体）。M9（最基础 HTTPS）已计划并写入文档，但尚未
开始；之后是 M10（HTTP 代理）；JavaScript 子集已暂缓。** 快照与接管指南见
[handoff.md](handoff.md)。

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
- 已达成验收：对 `file://` 和对一个 loopback HTTP 服务器得到逐字节相同的 dump；错误路径以非零退出；`net.url`、`net.http`、`net.file`、`shell.dump_body_file` 和 `shell.dump_body_missing_file` 在 GCC 和 Clang 下、警告视为错误时通过。M2 时尚无 TLS（HTTPS 计划在 M9）。
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

## M6 — 绘制：第一个真实页面 ✅（已完成）

- `render`：扁平显示列表（`FillRect`、`DrawText`、`PushClip`/`PopClip`）和确定性软件光栅化器——SrcOver 矩形填充、整数裁剪栈，以及手写的 8x16 ASCII 位图字体（按最近邻单元缩放，无抗锯齿、无字体依赖）。
- `paint` 模块：把盒树转换成显示列表——按前序先画元素背景，再为每个文本盒用计算出的颜色/字号画一次文本。它不做光栅化，`render` 仍然完全不了解 HTML/CSS。
- `shell`/CLI：可选的位置参数页面 URL（`toybrowser [options] [url]`），由窗口模式和 `--screenshot` 使用；页面被抓取、解析、计算样式、按 surface 宽度布局、绘制并呈现/写成 PPM。不给 URL 时两种模式都保留 M1 占位页。
- 已达成验收：`render.paint`、`paint.display_list`、`shell.screenshot_page` 与位置参数 URL 的 CLI 测试在 GCC 和 Clang 下、警告视为错误时通过（共 37 个测试）；200x100 surface 上的像素端到端测试验证了红色标题/黑色段落的几何；GCC/Clang 产出逐字节相同的 PPM；窗口显示同一页面。
- M6 已知限制：所有绘制都是整数设备像素，无抗锯齿、无子像素布局（1 CSS px = 1 设备 px）；没有 border（图片在 M8a 到来，真实字体自 M8b 起是 opt-in）；canvas 始终由 shell 填白，`html`/`body` 的 `background-color` 只绘制其元素盒（不会传播为 canvas 背景）；绘制顺序是前序，没有层叠上下文/z-index；文本只有 ASCII 位图字形（其他码点渲染 `?`，链接只有颜色没有下划线，因为属性集里没有 `text-decoration`）；溢出只被 surface 裁剪——滚动与导航在 M7；光栅化器的工作量受裁剪区域限制，对抗性字号也不例外。

## M7 — 导航 ✅（已完成）

- `shell::Browser`：一个 headless 浏览器会话，持有当前页面（最终 URL、DOM、样式、盒树）、
  导航历史、垂直滚动偏移与绘制，因此点击、历史、滚动与标题都能不开窗口测试。
- `net::ResolveUrl`：按 RFC 3986 把相对引用解析到当前页面的最终 URL（http/file 子集；file
  结果会丢弃本 MVP 无法承载的 query/fragment）。
- `layout`：`HitTest`（包含某点的最后绘制的盒）与 `ContentHeight`（可滚动范围）；`paint` 用
  视口 `PushClip`/`PopClip` 对包裹页面，并把每条命令按滚动偏移平移。
- `platform`：归一化输入事件（窗口像素中的左键点击、滚轮格数、带修饰键的映射按键）与
  `SetTitle`；X11 后端映射按键/keysym 并发布 `_NET_WM_NAME`/`WM_NAME`。
- `shell` 事件循环：点击链接导航，滚轮/方向键/PageUp/PageDown/Home/End 滚动，Alt+Left/Right
  前进后退，窗口标题跟随 `<title>`（没有时回退到 URL）。`--screenshot` 通过同一个会话在滚动 0
  处渲染一帧；`--dump-*` 模式不变且 headless。
- 一个提交的三页 fixture 站点（`tests/fixtures/nav_site/`）可在窗口中浏览，并驱动 headless
  `shell.navigation` 测试。
- 已达成验收：`net.url_resolution`、`layout.hit_test`、更新后的 `paint.display_list`、
  `shell.navigation` 以及所有既有测试在 GCC 与 Clang 下、警告视为错误时通过（共 40 个测试）；
  滚动 0 的截图与所有既有渲染与 M6 逐字节一致；本地多页站点可在窗口中浏览（点击、前进/后退、
  滚动、标题）。
- M7 已知限制：只有垂直滚动（没有水平滚动、没有滚动条）；滚轮一格固定 3 行（48 px），滚动为
  整数设备像素；导航会重新加载页面（无缓存），因此前进/后退把滚动偏移重置为 0，而不是恢复它；
  fragment 导航不会滚动到锚点（纯 fragment 链接是 no-op）；只有 `http`/`file` 链接会导航
  （不支持的 scheme 在 stderr 报错）；加载失败保留当前页面并在 stderr 报错；没有 hover/active
  链接样式、光标变化、表单或页内错误页。

## M8a — PNG 图片 ✅（已完成）

- `render`：`Image`（直通 alpha 的 ARGB、尺寸上限）与 `DecodePng()`，把已批准的 libpng 依赖封装在
  冻结接口之后（libpng 类型不离开模块；失败是普通错误；尺寸在分配前校验）。显示列表新增
  `DrawImage`，共享图片所有权，使用冻结的最近邻整数源映射与既有 SrcOver/裁剪语义；没有图片的
  页面产出与 M8a 之前完全相同的命令列表。
- `layout`：带调用方提供的固有尺寸（`LayoutOptions::replaced_sizes`，按 DOM 节点 id 索引）的行内
  元素成为原子行内 replaced 盒：CSS `width`/`height`（px）覆盖固有尺寸，只指定一侧时保持固有
  宽高比，盒整体换行、抬高行高并顶部对齐。`layout` 仍然不依赖 render，没有条目的页面保持原有
  几何与 `<tag> inline` dump 格式。
- `paint`：对节点 id 有解码图片的行内 replaced 盒，在其背景之后、子内容之前发出一条 `DrawImage`；
  空图片映射保持 M7 的列表。
- `shell`：私有页面加载器（最终 URL、DOM、样式、盒树、replaced 尺寸与图片映射）把每个
  `<img src>` 相对页面最终 URL 解析，用既有 `net::Fetch` 抓取（仅 http/file），用
  `render::DecodePng` 解码；失败或缺失的图片被静默跳过（没有 broken-image UI、没有错误输出、
  没有图片缓存）。`shell::Browser` 持有页面及其图片并绘制它们，resize 时重新布局而不重新抓取；
  `--dump-boxes` 使用同一个加载器，因此保留的图片盒会出现在盒 dump 中。CLI 不变。
- Fixtures：像素内容有文档的手工生成 PNG（`tests/fixtures/images/`）与提交的
  `tests/fixtures/img_site/` 演示页。
- 已达成验收：`render.image`、`layout.replaced`、`shell.images`、图片 CLI 检查与所有既有测试在
  GCC 与 Clang 下、警告视为错误时通过（共 46 个测试）；无图片的 dump 与截图与 M7 逐字节一致；
  GCC 与 Clang 产出逐字节相同的图片 PPM；窗口显示图片页面。
- M8a 已知限制：只支持 PNG（libpng 能转换成 8-bit RGBA 的全部格式；没有 JPEG/GIF/WebP/SVG），
  没有 `background-image`、`srcset`、`alt` 文本或 broken-image 提示，没有图片缓存（同一 URL 在
  每个 `<img>` 处各抓取一次），HTML `width`/`height` 属性被忽略（只用 CSS px），
  `display:block` 的 replaced 元素以及 replaced 元素的 margin/padding 不支持，replaced 盒顶部
  对齐（没有基线对齐，同 M5），缩放为无抗锯齿的最近邻，元素不可见时也会抓取图片（没有懒加载），
  图片 URL 在 M8a 时只支持 `http`/`file`（M9 计划解除这一限制），并且 `--dump-dom`/`--dump-style`
不抓取图片。

## M8b — 真实字体 ✅（已完成）

- `render`：`Font` 通过已批准的 freetype 依赖在冻结的 pimpl 接口后加载 TTF/OpenType face（公开
  头文件不出现 freetype/harfbuzz 类型），并暴露设计单位度量与 harfbuzz 整形后的 advance 宽度。
  显示列表新增 `DrawTextWithFont`：real-font 文本用 freetype 8-bit 覆盖率做抗锯齿，使用冻结的
  逐字形整数放置公式放在命令基线上，用既有 SrcOver 公式混合并受裁剪限制。real-font 的有效字号
  被钳制到 `kMaxRealFontSizePx`（4096 px），用于度量与绘制，因此对抗性 CSS 无法让字形位图无界
  分配。内置位图 `DrawText` 路径不受影响。
- `paint`：选中真实字体时，文本盒变成 `DrawTextWithFont`，基线为 `round(box.y) - scroll_y +
  round(font.ascent)`；没有字体时精确产出 M8b 之前的位图命令。
- `shell`：`--font PATH` 加载字体文件，`--font-family NAME` 通过已批准的 fontconfig 依赖解析
  字体族（最佳匹配；未知字体族回退），解析出的字体只加载一次，并由布局度量（经既有
  `MeasureText` 回调）与绘制共享。两个标志互斥、与 `--dump-body`/`--dump-dom`/`--dump-style`
  组合是用法错误，字体无法加载是运行时错误（退出码 1）。整个会话只有一个字体；不给标志时仍用
  内置位图字体，使所有 M8b 之前的输出逐字节不变。
- Fixtures：提交的自造最小 TTF（`tests/fixtures/fonts/toy_test.ttf`，度量/字体族有文档）与
  real-font 演示页（`tests/fixtures/font_site/`）。测试从不依赖已安装字体；fontconfig 路径用
  测试局部的 `FONTCONFIG_FILE`（只指向 fixture 字体目录）测试。
- 已达成验收：带真实 TTF 文本与图片的页面可以渲染（headless 截图与真实窗口）；`render.font_ttf`、
  扩展后的 `render.paint`、`shell.fonts` 与七个 CLI/fontconfig 检查以及所有既有测试在 GCC 与
  Clang 下、警告视为错误时通过（共 55 个测试）；不给字体标志时所有 M8b 之前的 dump 与截图与
  M8a 逐字节一致；位图与 real-font 路径的 GCC/Clang PPM 都逐字节相同。
- M8b 已知限制：real-font 是 opt-in（默认仍是 ASCII 位图字体），整个会话一个 face（没有按元素的
  CSS `font-family`/`font-weight`/`font-style` 映射、没有 fallback 链、没有 `@font-face`/字体
  下载），除 harfbuzz 默认 buffer 整形外没有额外文本整形特性（没有 bidi 重排、竖排或复杂脚本
  分段），fontconfig 总是返回最佳匹配，因此未知字体族会静默回退，只支持 face 0（忽略 TTC/face
  index），缺失码点渲染字体的 `.notdef` 字形，real-font 字号超过 4096 px 时等同 4096 px，且不
  支持整形 advance 之外的亚像素定位/kerning。位图回退仍只有 ASCII。
## M9 — 最基础 HTTPS（已计划；OpenSSL 已批准，尚未开始）

- 目标：抓取绝对 `https://` URL，让 HTTPS 站点的页面与图片走现有 pipeline 渲染。MVP 定义刻意很小：
  **能连上并发出请求**——不要过度构建。
- MVP 范围：
  - `net` URL 层：`https` scheme（默认端口 443），解析/序列化/base 解析；链接、导航、重定向与图片
    加载自动复用。
  - `net` TLS：现有阻塞 `Fetch()` 之后接 OpenSSL 客户端，通过一个很小的字节流抽象与纯 HTTP 共享
    HTTP/1.1 请求/响应代码。SNI、主机名校验与证书校验（OpenSSL 默认信任库，即系统 CA；标准
    `SSL_CERT_FILE`/`SSL_CERT_DIR` 覆盖仍可用于特殊情况）。证书始终校验，没有 insecure 模式。
  - 超时：TCP 连接沿用 `connect_timeout_ms`；TLS 握手与 TLS 读沿用 `read_timeout_ms`。M9 不新增
    `FetchOptions` 字段。
  - `shell`：预计无需改动公开 API 或 CLI——`--dump-body`、`--dump-*`、`--screenshot`、窗口模式与
    导航通过现有代码路径接受 `https://`。
  - 测试（确定性、不依赖公网）：提交的自签测试 CA + `localhost` 叶证书/私钥（仅测试用，README 记录
    重新生成方法）与进程内 TLS loopback 服务器；`net` 测试覆盖成功、不受信任证书、主机名不匹配、
    超时与 http<->https 重定向；shell 测试通过 TLS 提供提交的 HTML+PNG 页面并断渲染像素。
- 冻结的设计决策（kickoff 前冻结，记录在 [decisions.md](decisions.md)）：
  - scheme 集合为 `file`、`http`、`https`；`https` 默认端口 443；`ResolveUrl`/`SerializeUrl` 遵循
    与 http 相同的 RFC 3986 子集。
  - 重定向用 `net::ResolveUrl` 解析（支持相对与协议相对 `Location`）；重定向目标必须是 http/https，
    允许二者互切；拒绝 file 目标。
  - 不做 ALPN/HTTP/2：保持 HTTP/1.1 + `Connection: close`；不做压缩、cookie、会话、HSTS、混合内容
    拦截、客户端证书、TLS 会话复用或 HTTP 缓存。
  - M9 中 `FetchOptions` 保持不变；失败是可读的 fetch 错误（CLI 退出码 1）。
- 验收：所有 55 个既有测试加新的 URL/TLS/https 测试在 GCC 与 Clang 下、警告视为错误时通过；本地
  HTTPS 页面（含图片）可 headless 渲染；证书、主机名与超时失败给出明确错误；`http://`/`file://`
  的 dump 与截图逐字节不变；真实站点直连检查是手动的，取决于网络（受限网络需要 M10 代理）。
- M9 之后的已知限制：没有 HTTP/2、压缩、cookie、代理（M10）、依赖 JS 的站点或 HSTS；真实站点渲染
  仍受现有 HTML/CSS 子集限制。

## M10 — HTTP 代理（M9 之后计划）

- `http://` 通过 HTTP 代理的 absolute-form 请求抓取；`https://` 通过代理 `CONNECT` 隧道后再做 TLS
  握手。
- CLI `--proxy host:port` 加标准 `http_proxy`/`https_proxy` 环境变量回退；进程内假代理测试。这是在
  直连受限网络上测试真实站点的关键。
- 范围与契约在那一轮开始时冻结；预计不需要新依赖。

## 暂缓 —— JavaScript 子集

- 用户于 2026-10-31 暂缓 JavaScript 子集；它不是下一个里程碑。它仍是一个大的可选里程碑（词法
  分析器、解析器、小型解释器、DOM 绑定与事件处理器）；没有用户明确的优先级决定不要开始。

## M11+ — 机会性工作

更多 CSS（flex、表格）、表单、更好的文本处理、性能、一致性测试套件。每一项都只在前一个切片干净、且用户同意优先级之后才开始。

## Backlog — 计划中的后续工作

- **更多图片格式与背景图**——JPEG/GIF/WebP、`background-image`、`srcset`、图片缓存与懒加载；这些都在 M8a 之后按需排期，不阻塞任何里程碑。
- **CSS 字体选择与更丰富的文本**——把 CSS `font-family`/`font-weight`/`font-style` 映射到字体栈，加入 fallback 链、`@font-face`/字体下载与更丰富的整形；M8b 的 CLI 选择刻意是每会话一个 face。
- **测试辅助代码合并**——较旧的测试可执行文件里仍然重复着每个文件各自的 `Check`/`g_failures` 测试支架。header-only 的 `tests/support/check.h`（接口目标 `toy_test_support`）已在 M5 引入，新测试都在使用；旧文件只在被改动时迁移。不引入框架依赖。

## 规则

- 前一个里程碑的演示可用之前，不要开始下一个里程碑。
- 绝不让里程碑因等待依赖审批而阻塞：先交付 headless 或自实现切片，审批通过后再升级。
- 任何里程碑都可以被用户重新划定范围。MVP 永远优先于完整性。
- headless 优先：任何可见或可观察的结果都必须能在不开窗口的情况下检查（dump 命令或截图），这样项目可以完全 headless 地开发和测试。
