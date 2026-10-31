> 本文为中文翻译，英文原版为准：[roadmap.md](../roadmap.md)。

# 路线图

每个里程碑都是一个最小垂直切片：小、可构建、可演示、可测试。一个里程碑在其演示命令可用、新逻辑有测试、文档仍然准确时才算完成。

**当前状态：M0–M14 已完成（M14 = CSS 兼容批次 1：M14a 值与排版、M14b 盒装饰与约束），
M17（JPEG 图片支持）应维护者要求在顺序之外提前交付。M15（flexbox/position/float 子集）是下一个
计划里程碑。HTTP 代理（M16）与 JavaScript 子集已延后。**
快照与接管指南见 [handoff.md](handoff.md)。

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
- M4 已知限制：没有外部 `<link>` 样式表（计划为 M12），没有 at-rule 内容，没有伪类/伪元素，没有属性选择器，没有 `rgb()`/`hsl()`/`calc()`、`inherit`/`initial` 关键字或 CSS 变量，没有 `%`/`pt`/`rem` 单位，没有 `margin: auto`，命名颜色使用标准值（例如 green = #008000）。

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
  `<img src>` 相对页面最终 URL 解析，用既有 `net::Fetch` 抓取（M8a 时仅 http/file；M9 起含 https），用
  `render::DecodePng` 解码；失败或缺失的图片被静默跳过（没有 broken-image UI、没有错误输出、
  没有图片缓存）。`shell::Browser` 持有页面及其图片并绘制它们，resize 时重新布局而不重新抓取；
  `--dump-boxes` 使用同一个加载器，因此保留的图片盒会出现在盒 dump 中。CLI 不变。
- Fixtures：像素内容有文档的手工生成 PNG（`tests/fixtures/images/`）与提交的
  `tests/fixtures/img_site/` 演示页。
- 已达成验收：`render.image`、`layout.replaced`、`shell.images`、图片 CLI 检查与所有既有测试在
  GCC 与 Clang 下、警告视为错误时通过（共 46 个测试）；无图片的 dump 与截图与 M7 逐字节一致；
  GCC 与 Clang 产出逐字节相同的图片 PPM；窗口显示图片页面。
- M8a 已知限制：M8a 时只支持 PNG（JPEG 在 M17 加入；GIF/WebP/SVG 仍不支持），
  没有 `background-image`、`srcset`、`alt` 文本或 broken-image 提示，没有图片缓存（同一 URL 在
  每个 `<img>` 处各抓取一次），HTML `width`/`height` 属性被忽略（只用 CSS px），
  `display:block` 的 replaced 元素以及 replaced 元素的 margin/padding 不支持，replaced 盒顶部
  对齐（没有基线对齐，同 M5），缩放为无抗锯齿的最近邻，元素不可见时也会抓取图片（没有懒加载），
  图片 URL 在 M8a 时只支持 `http`/`file`（https 在 M9 加入），并且 `--dump-dom`/`--dump-style`
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
## M9 — 最基础 HTTPS ✅（已完成）

- 目标已达成：抓取绝对 `https://` URL，让 HTTPS 站点的页面与图片走现有 pipeline 渲染。MVP 标准仍是
  “能连上并发出请求”。
- `net` URL 层：`https` scheme，默认端口 443，与 http 相同的 RFC 3986 authority/端口/path 规则；
  `ParseUrl`/`ResolveUrl`/`SerializeUrl` 接受并可往返序列化；scheme 相对引用继承 base scheme，
  显式 http<->https 引用允许。
- `net` 传输层：保持阻塞 `Fetch()` 不变，内部使用已批准的 OpenSSL（`find_package(OpenSSL REQUIRED)`，
  PRIVATE 链接到 `toy_net`）；纯 HTTP 与 TLS 通过私有字节流抽象（`Stream`、`SocketStream`、
  `TlsStream`）共享同一套 HTTP/1.1 请求/响应代码，因此 body 上限、header/chunked 解析与超时都在
  一处。进程级 `SSL_CTX` 在第一次 https 抓取时惰性创建，来自 OpenSSL 默认校验路径；`SSL_VERIFY_PEER`
  始终开启，DNS 主机发送 SNI（IPv4 字面量不发送），证书链与主机名校验覆盖 DNS 名与 IPv4 字面量，
  没有 insecure 模式。TCP 连接用 `connect_timeout_ms`；TLS 握手与所有 TLS I/O 用
  `read_timeout_ms`；干净与非干净的 EOF 都视为流结束（截断仍由 Content-Length/chunked 捕获）；TLS
  路径在进程内忽略一次 `SIGPIPE`，坏掉的对端无法终止浏览器。
- 重定向：`Location` 用 `net::ResolveUrl` 解析（绝对、相对、根相对、协议相对、scheme 相对）；
  允许 http<->https 目标，拒绝 `file` 目标，`max_redirects` 不变。
- `shell`：无需改动 API 或 CLI——页面抓取、链接导航（https 链接现在可导航）、重定向与图片加载都复用
  现有 `net` 调用；页面加载器、`--dump-*` 模式、`--screenshot`、窗口模式与导航自动接受 `https://`。
- 测试：提交仅测试用的 TLS fixture（`tests/fixtures/tls/`：CA、`localhost` 叶证书、无关的不受信任
  叶证书；README 记录重新生成方法）与 header-only 的进程内 TLS loopback 服务器
  （`tests/support/tls_server.h`）。`net.tls` 覆盖成功 GET、不受信任证书、主机名不匹配、握手超时、
  https->https / http->https / https->http 重定向与 body 上限；`net.http` 增加相对与协议相对重定向
  覆盖，并删除过时的“https 重定向是错误”用例；`net.url`/`net.url_resolution` 覆盖 https 解析、序列化
  与解析；`shell.https` 通过 TLS 提供提交的 HTML+PNG 页面（`tests/fixtures/https_site/`），断言
  URL/标题/内容高度、文本与图片像素、请求顺序以及不受信任证书失败路径。M7 导航 fixture 的
  “Unsupported page” 链接现在故意使用 `ftp://`，任何测试都无法访问公网。
- 已达成验收：55 个 M9 之前的测试加新的 URL/TLS/https 测试在 GCC 与 Clang 下、警告视为错误时通过
  （共 57 个测试）；本地 HTTPS 页面（含图片）可 headless 渲染；证书、主机名与超时失败给出明确错误；
  所有 M9 之前的 `http`/`file` dump 与截图与 M8b 基线逐字节相同，GCC/Clang 的 PPM 也逐字节相同；
  手工公网站点检查（原始 body、DOM dump、截图）可用。真实站点检查是手动的、依赖网络；受限网络仍需
  M16 代理。
- M9 已知限制：只有 HTTP/1.1（无 ALPN/HTTP/2）、无压缩（不发送 `Accept-Encoding`）、无
  cookie/会话、HSTS、混合内容拦截、客户端证书、TLS 会话复用或 HTTP 缓存；无代理（M16）；IPv6 字面量
  https URL 仍不被 URL 解析器支持；信任策略就是 OpenSSL 默认信任库加标准环境变量覆盖（无 pinning，
  也没有 insecure 模式）；TLS context 每个进程惰性创建一次；真实站点渲染仍受现有 HTML/CSS 子集限制；
  公网细节从不提交，真实站点检查保持手工。

## M10 — 自动字体选择 ✅（已完成）

- 目标已达成：不传任何字体标志时，浏览器像普通浏览器一样自己选择字体。计算出的 CSS
  `font-family` 列表、`font-weight` 与 `font-style` 通过已批准的 fontconfig 库匹配；初始字体族为
  `serif`；文本盒按字符覆盖切成 run，首选字体缺少的字符（拉丁页面里的汉字，或汉字页面里的拉丁词）
  回退到字符集覆盖它的已安装字体；页面语言（`<html lang>`）作为 fontconfig 的语言提示传入。
- `style`：`font-family` 成为可解析、可继承的计算属性（逗号列表、带引号族名、通用族规范化）；UA
  样式表增加 `b/strong` 粗体、`i/em` 斜体与 `code/kbd/samp/pre` 等宽字体；`--dump-style` 增加
  `font-family=` 字段。
- 布局与绘制共享同一个会话级 `Fonts` 对象，因此文本盒在度量与绘制中选中的字体一致：
  `TextMetrics` 增加 `ascent_px`；度量回调收到文本的计算样式；文本盒携带 `baseline_offset_px`
  （行 ascent，使同一行不同字体共享基线）；paint 接收 `FontRunsProvider`，返回每个 run 的真实
  字体，或返回空以走内置位图路径。
- 中文/可读文本支持：layout 现在把文本切成断行单元：ASCII/U+3000 空白照旧分隔单词，非 CJK 文本
  仍按词分组，CJK 字符各自成为断行机会（汉字、假名、谚文、注音、CJK 标点、全角形式），并实现简化
  禁则（收尾标点不出现在行首、起始标点不出现在行尾）。因此无空格的中文段落会换行、可阅读。非法
  UTF-8 宽松处理，不会崩溃或死循环。
- `--font PATH`（单一精确 face，无回退）与 `--font-family NAME`（首选字体族，现在也遵循计算出的
  weight/style，并保留字符回退）仍是显式覆盖。没有可用系统字体或 fontconfig 失败时，浏览器退化到
  内置位图字体；页面加载不会因为缺字体失败。
- 测试保持确定性：提交的自造 fixture 字体（`tests/fixtures/fonts/`：M8b 的常规 face 加新的粗体、
  斜体 face 和一个小汉字 face，均有文档）与生成的测试 fontconfig 配置。空 fontconfig 配置逐字节
  复现所有 M10 之前的位图 golden；fixture 配置覆盖自动选择（默认 serif、通用族、weight/style、
  未知族回退）与汉字回退；`shell.cjk_*` 提供提交的中文页面并断言逐单元文本盒、换行与绘制像素；
  `layout.cjk_line_break` 覆盖分词与标点禁则。
- 已达成验收：所有 M10 之前的测试加新的 style/layout/paint/shell 测试在 GCC 与 Clang 下、警告视为
  错误时通过（共 66 个测试）；不传字体标志的页面用系统 serif/粗体/斜体渲染英文、用汉字回退字体并
  换行渲染中文；空 fontconfig 的位图路径与 M10 之前基线逐字节相同，GCC/Clang 在两条路径上的 PPM
  也逐字节相同；手工检查可用系统字体渲染拉丁与中文页面。
- 已知限制：没有 `@font-face`/webfont 下载（只用已安装字体）；跨文本节点的标点不能粘连
  （`中<b>文</b>。` 里的 `。` 仍可能出现在行首）；每个 CJK 断行单元一个文本盒（MVP 可接受，受
  16 MiB body 上限约束）；只实现简化 UAX#14 子集（无完整断行、无泰语/高棉语/老挝语词典断行、无
  连字符断行）；没有 bidi/RTL 排版（阿拉伯语/希伯来语按逻辑顺序渲染，不做视觉重排），复杂脚本
  除 harfbuzz 默认逐 run 整形外无额外处理；其他语言通过通用 fontconfig 字符覆盖尽力支持（英文与
  中文是明确支持的语言）；不做合成粗体/斜体（缺粗/斜 face 时回退常规）；只支持 TTC face 0；没有
  `font-stretch`/`font-variant`/`font-feature-settings`；语言只取根 `<html lang>`；系统字体输出
  按设计依赖机器（测试通过 `FONTCONFIG_FILE` 固定）。

## M11 — 性能 ✅（已完成）

- 目标已达成：让大页面响应流畅且不改变任何渲染像素。实测起点（800×600、自动系统字体、压力页：
  4000 段/8.1 万文本盒的英文页与 1200 段/4.8 万汉字盒的中文页）：单帧绘制 1.3 s（1 万词）到
  10.6 s（8.1 万盒），布局 0.27–2.1 s，滚动与 resize 每帧都重做全部工作；任意滚动位置只有 0.4%
  的文本盒与视口相交。位图页面因为默认构建未优化而慢了 3–7 倍；窗口循环在每次 Expose 都重排、
  每个 resize 事件都重排且不合并 X11 事件。
- P0 构建：未指定 `CMAKE_BUILD_TYPE` 时默认使用优化构建（RelWithDebInfo），并加
  `-ffp-contract=off` 保证浮点确定性，使布局几何在不同构建类型与编译器间逐字节一致。仅这一步就把
  位图路径的加载/截图时间降低了 3–7 倍。
- P1 文本缓存（render + shell）：每个字体/字号复用一个 harfbuzz font/face（原来每次 shaping 新建
  hb_face，导致 harfbuzz 的 shape-plan 缓存失效）、复用 buffer；为 shaped run 与 glyph 覆盖率位图
  增加有界 LRU 缓存（16 MiB/16k 与 32 MiB/16k entries，分配失败视为 miss，逐出不影响输出）；shell
  侧增加无分配的解析样式快速路径（稳定 style id），以及文本度量 LRU（32 MiB/100k）与字体 run
  LRU（16 MiB/100k）；位图退化路径刻意绕过这些缓存。
- P2 帧/窗口：`paint` 增加视口裁剪（背景/图片按精确目标矩形相交裁剪；文本盒四周扩大
  `ceil(2 * font_size_px)` 覆盖 glyph overhang/斜体/AA；在样式/图片/字体 run 查找之前裁剪；保留
  恰好一个 viewport clip 对与命令顺序；渲染像素与不裁剪完全一致）；窗口循环修正：Expose 不再触发
  布局，resize 只有尺寸真正变化才重排，X11 后端合并连续的 Expose/ConfigureNotify 队列事件（输入与
  close 事件永不合并、丢弃或乱序），复用绘制 surface 而不是每帧分配。
- 已达成验收：所有 66 个 M11 之前测试与字节兼容/跨编译器门禁保持通过；fixture 字体与位图路径在
  33 个滚动参考帧上与 M11 基线逐字节相同；窗口打开只布局一次。在 8.1 万文本盒压力页上实测
  （800×600、自动系统字体、开发机）：单帧绘制 **10.6 s -> 3.5 ms**，滚动 **11.0 s -> 3.1 ms**，
  resize 帧 **11.0 s -> 3.0 ms**，布局 **2.1 s -> 31 ms**，页面加载 **2.2 s -> 69 ms**；
  1 万词页面绘制 **1.38 s -> 0.89 ms**、布局 **267 ms -> 3.3 ms**、加载 **290 ms -> 30 ms**；
  中文压力页绘制 **3.2 s -> 2.3 ms**、加载 1.3 s -> 142 ms（剩余加载时间主要是首次 CJK 字体加载）。
  位图路径也因裁剪去掉了视口外命令而变快，布局没有回归。X11 真实窗口烟测通过：resize 后无残留帧、
  40 个 configure 事件合并为 2 次重排、首次 Expose 只布局一次、关闭干净。
- M11 之后的已知限制：第一帧仍有冷缓存成本；裁剪仍每帧遍历整棵盒树（便宜，但还没有空间索引）；
  resize 仍是全量重排（度量缓存使其很快，但超大页面拖动时未必完全顺滑；没有增量布局）；渲染仍是
  单线程（没有后台布局/绘制，也没有并行资源加载）；图片加载与真实站点网络/HTTP 性能
  （keep-alive、gzip、并行抓取、图片缓存/懒加载）不在本轮；窗口仍每帧呈现整个 surface
  （XShm/dirty-rect 留待后续，需要新依赖审批）；GPU 渲染刻意不使用（实测填充率不是瓶颈，成本在
  CPU 算法；是否重新评估见 decisions）。

## M12 — 外部样式表 ✅（已完成）

- 目标已达成：通过 `<link rel="stylesheet" href="...">` 引用 CSS 的页面，能把这些规则与内联
  `<style>`、`style` 属性放进同一套级联后应用。这是当前引擎与真实页面之间最大的功能性缺口：M4 刻意
  留下它，而大多数真实站点把样式放在外部文件里。对照 `https://www.python.org/` 的手工检查现在会应用
  站点的排版与深色配色，而不是渲染未套样式的文本。
- `style`：公开 `LinkedStyleSheet { dom::NodeId link_node; std::string text; }` 与双参数
  `ComputeStyles(document, linked_sheets)`。一次前序遍历把文档自己的 `<style>` 元素与每个调用方提供
  的外部样式表合并到其 `<link>` 元素的文档位置，因此同优先级的文档序 tie-break 与浏览器一致；
  specificity、`!important`、继承与内联 `style` 规则不变。单参数重载传入空列表委托，保持 M12 之前的
  输出与每页开销；`style` 仍然没有任何 I/O 或网络代码。
- `shell`：新的私有 `LoadStyledDocument()` 在正常页面加载（窗口、`--screenshot`、`--dump-boxes`）与
  `--dump-style` 之间共享同一条“抓取 → 解析 → 发现/过滤/抓取样式表 → 计算样式”路径；
  `--dump-dom`/`--dump-body` 不变且不抓取样式表。一次迭代式前序遍历（head 或 body 都算）发现
  `<link>`；MVP 过滤规则是：`rel` 分词后含 `stylesheet` 且不含 `alternate`、没有 `disabled` 属性、
  `media` 为空/`all`/`screen`、`type` 为空/`text/css`、非空 `href` 用 `net::ResolveUrl` 相对重定向后
  的最终 URL 解析。每个适用的 link 每次出现抓取一次（不去重/缓存），使用 `net::Fetch` 默认参数
  （16 MiB body 上限、连接/读取超时），只解析状态码 200..299 的 body；所有被过滤/无法解析/失败/
  非 2xx 的样式表都静默跳过（不写 stderr，页面仍成功加载），与 M8a 起缺失图片的处理完全一致。
- Fixtures/测试：提交的 `tests/fixtures/css_site/`（子目录相对样式表、文档序交错、过滤、失败/
  disabled/alternate/media/type/ftp/空 href 链接、重复链接、head 与 body 链接、无样式表与全被过滤
  页面）及其有文档的期望计算值/几何/颜色；`style.external_sheets`、`shell.external_css`（含进程内
  loopback HTTP 重定向、绝对 URL、相对最终 URL 解析、失败与不去重请求数等用例，使用新的 header-only
  `tests/support/http_server.h`）与 `--dump-style` CLI 检查；共 70 个测试。
- 已达成验收：所有 66 个 M12 之前测试加 4 个新测试在 GCC 与 Clang 下、警告视为错误时通过（共 70 个
  测试）；不带 `<link>` 的页面在 dump、退出码与 PPM 像素上与 M12 之前逐字节相同（20 个用例的
  byte-compat 门禁）；GCC 与 Clang 输出逐字节相同；手工真实站点检查（`https://www.python.org/`）
  显示外部排版与颜色生效；没有新依赖。
- 本轮之后的已知限制：没有 `@import`、media queries（包括真实站点常用的 `media="not print, ..."`
  这类逗号列表，本 MVP 会跳过）、alternate/disabled 样式表选择、带参数的 `type` 值、`url(...)`
  资源、CSS `@charset` 或跨域规则；样式表仍由 M4 的 CSS 子集解析，因此大量真实规则被忽略
  （M14/M15）；所有样式表加载仍是串行的并使用默认超时（M13 的 keep-alive 会降低连接开销）；每次
  出现单独抓取，还没有样式表缓存。

## M13 — 资源加载管线 ✅（已完成）

- 目标已达成：含大量资源的页面加载时间现在与字节数成正比，而不再为每个资源付出新连接（与 TLS 握手）
  成本。实测起点是图片较多的真实页面墙体时间约 20 s、CPU 仅约 0.1 s，因为每个 `<img>`（以及 M12
  起的每个样式表）都在新连接上串行、未压缩、无缓存地抓取。
- `net`：新增公开 `net::Client`（pimpl、单线程），每个 origin（scheme/host/port）最多保留一条空闲
  HTTP/1.1 连接并跨请求复用。它增量地解析响应：有界读取 header，然后精确读取 `Content-Length` 或
  完整 chunked body（含 trailer），读到的多余字节留在池化连接上供下一请求使用。持久连接遵循
  HTTP/1.1（无 `Connection: close`）或 HTTP/1.0（`Connection: keep-alive`）规则并要求长度定界
  （持久响应缺少长度定界是可读错误），遇到 `Connection: close` 则关闭。它发送
  `Accept-Encoding: gzip`，用已批准的 zlib 透明解压 gzip body（`identity` 直接通过，其他编码是
  错误；原始与解码后 body 都受限，header 原样保留）。池化连接在收到任何响应字节前失败时，会在新连接
  上重试一次；超时、新连接失败以及已收到响应字节后的失败都不重试。重定向复用同一个池（同 origin
  复用，跨 origin 为各自 origin 保留空闲连接），`file://` 与一次性抓取一致。公开的一次性
  `net::Fetch()` 保持完全相同的可观察行为（同样的请求字节与 `Connection: close`、不协商压缩、同样的
  body/超时/重定向/错误语义），仍是便捷 API。
- `shell`：`LoadPage()` 保持签名，并为整个加载创建一个 `net::Client`；页面、所有外部样式表与所有
  图片都经它抓取，因此页面及其资源按 origin 复用连接。按解析后序列化图片 URL 索引的页内图片缓存让
  每个唯一 URL 最多抓取并解码一次：重复 `<img>` 共享同一个解码后的 `render::Image` 与固有尺寸，
  失败结果在本次加载内被记住，重复的坏图片不会重试。样式表语义保持 M12（每个适用 `<link>` 出现
  各抓取一次；样式表不去重/不缓存），`RelayoutPage` 仍不重新抓取，也没有跨加载/跨会话缓存。
- 测试（全部离线、确定、不断言耗时）：`net.keep_alive` 覆盖同 origin 单连接复用（Content-Length
  与 chunked body）、`Connection: close`、两个 origin、过期连接重试、新连接失败、
  `CloseIdleConnections`/析构、`file://`、同 origin 重定向、HTTP/1.0 持久规则、缺少长度定界错误与
  原始 body 上限；`net.gzip` 覆盖 Content-Length 与 chunked gzip、identity、损坏 gzip、不支持的
  编码、解码后大小上限，并证明一次性 `Fetch` 仍不发送 `Accept-Encoding`、也不解码；`net.tls_keep_alive`
  用一条 TLS 连接完成两次请求；`shell.resource_loading` 从 loopback server 提供页面、样式表与重复
  图片，断言精确的连接/请求数（一条连接、每个唯一图片 URL 一次抓取）、共享的解码图片对象与像素、
  重复缺失图片只抓一次、不同 URL 不会被过度共享，以及被强制关闭的空闲连接后的恢复。共享 loopback
  HTTP/TLS 测试服务器增加了持久连接模式与连接/请求计数；共 74 个测试。
- 已达成验收：所有 70 个 M13 之前测试加 4 个新测试在 GCC 与 Clang 下、警告视为错误时通过（共 74 个
  测试）；所有 M13 之前的 dump、退出码与 PPM 与 M12 基线逐字节相同（23 个用例的 byte-compat 门禁，
  包含 M12 的 `css_site` 页面）；GCC 与 Clang 输出相同；同一机器/网络实测 `https://www.python.org/`
  在 800x600 下从 2.18/2.07/2.02 s（M12 三次）降到 0.99/1.28/0.99 s（M13 三次），
  `https://www.w3.org/` 从 3.88 s 降到 1.79 s，前后截图逐字节相同；没有新依赖（zlib 早已批准，
  现在也由 `net` 直接链接）。
- 本轮之后的已知限制：只有 HTTP/1.1（无 HTTP/2/ALPN）、无 HTTP 缓存头、cookie、HSTS 或代理（M16）；
  没有并行/线程预取——资源仍是串行的，只摊薄了连接建立与传输成本（有限并行是以后基于实测的决定）；
  样式表 URL 不去重（每个适用 link 出现仍各抓一次；keep-alive 只省去每请求的连接成本）；图片去重只在
  单次页面加载内，不跨加载或跨会话；持久响应缺少长度定界是错误（204/304 等无长度状态码未特殊处理）；
  gzip 是唯一支持的内容编码；空闲池化连接不会主动探活（过期连接在下一次请求时被发现并最多重试一次），
  也没有空闲超时（连接存活到 Client 销毁）；显式发送 `Connection: keep-alive`。

## M14a — CSS 值与排版 ✅（已完成）

- 目标已达成：真实样式表依赖的高频值与排版属性现在生效：`%`/`rem` 单位、`rgb()`/`rgba()`/`hsl()`/
  `hsla()` 与完整 CSS 命名颜色集、`background` 简写的颜色部分、逻辑属性映射到物理边
  （`margin`/`padding` 的 block/inline 长写与简写、`inline-size`/`block-size`、
  `min/max-inline-size`/`min/max-block-size`）、`min/max-width`/`min/max-height`、`margin: auto`、
  `line-height`、`text-align`、`text-decoration: underline` 与 `visibility`。
- `style`：计算值模型新增 `Length`（px 或百分比；盒属性的百分比保持未解析交给 layout）、
  `AutoLength` margin、`LineHeight`（normal/number/px）、`TextAlign` 与 `Visibility`。字体相对单位
  在 style 内解析（`em` 相对元素自身字号、`%` 字号相对父元素、`rem` 相对根元素计算字号）；颜色解析
  十六进制、`rgb()`/`rgba()`、`hsl()`/`hsla()` 与命名颜色（文本 alpha 丢弃，背景 alpha 保留）；
  逻辑属性在 horizontal-tb 下映射到物理字段；`line-height`、`text-align`、`visibility` 与
  `text-decoration: underline` 按 MVP 契约继承。样式 dump 保留所有既有字段并在行尾追加
  ` min-width= min-height= max-width= max-height= line-height= text-align= visibility=
  text-decoration=`。
- `layout`：解析新长度（width/min-max-width/margin/padding 的百分比相对包含块内容宽度；百分比高度
  不支持、按 auto 处理），应用 min/max-width 钳制与 CSS 10.4 的 auto margin 居中（包括常见的
  `max-width + margin: auto` 包裹盒；过约束盒的 auto margin 保持 0），钳制 px min/max-height，
  用显式 `line-height` 贡献与 half-leading 基线排版行（`normal` 保持 M14 之前的逐字节几何），并在
  断行后按 `text-align: center/right` 平移整行（`justify` 按 left 排版）。visibility 不改变布局。
- `paint`：`visibility: hidden` 的盒不绘制背景/图片/文本但保留空间，隐藏盒内可见后代仍正常绘制；
  `text-decoration: underline` 在每个文本盒文本命令之后、同等裁剪下发出一个基线下方一行的设备像素
  `FillRect`。不使用这两个属性的页面构建与 M14 之前逐字节相同的命令列表。
- 测试/fixtures：提交 `tests/fixtures/css_compat_site/`（`values.html`、`typography.html`、
  `boxes.html`、`legacy.html` 加 README，记录期望计算值、盒几何与像素）；新增
  `style.values`、`style.typography`、`layout.style_properties`、`shell.css_compat`、
  `shell.dump_style_css_compat`、`shell.css_compat_screenshot` 并扩展 paint 显示列表测试；
  共 80 个测试。
- 已达成验收：所有 74 个 M14 之前测试加 6 个新测试在 GCC 与 Clang 下、警告视为错误时通过（共 80 个
  测试）；M14 之前的页面保持逐字节相同的盒 dump、截图与退出码（25 个用例门禁；style dump 只对追加
  字段做规范化）；GCC/Clang 输出相同；手工真实站点检查显示本批次生效：gnu.org 的红色导航栏
  （`background` 简写 + 居中导航文本）现在会绘制（其红色像素数 987 -> 66,443），w3.org 左侧导航
  获得行距与居中（页面高度 2440 -> 4048 px），python.org 应用了 275 个居中与 88 个 line-height
  元素（页面 5945 -> 6018 px），gnu.org 应用 274 个 line-height 加 76 个居中/4 个右对齐元素；
  没有新依赖。
- M14a 之后的已知限制：没有 border（`border*`）、`border-radius`、`box-sizing`、`overflow` 裁剪、
  `box-shadow`、`line-through`/`overline`、百分比高度/min-max-height、rem 的 `line-height`、现代
  空格分隔 `rgb()`/`hsl()` 语法、文本 alpha 或 `justify`；只实现 left/right/center 对齐、
  horizontal-tb 逻辑映射与 `background` 简写的颜色部分；除 block/inline/none 之外的 display 值仍被
  忽略（M15）；命名颜色覆盖 CSS Color 4 集，但颜色函数只解析旧式逗号语法。真实站点的剩余视觉差距
  主要来自 border（调查的样式表中 385 条声明，另有 121 条 `border-radius`）与 position/float
  （M15）。

## M14b — 盒装饰与约束 ✅（已完成）

- 目标已达成：剩余高频盒装饰现在生效。`border`/`border-*` 的宽度、样式与颜色（长写与简写）参与盒
  模型并在四边绘制；`box-sizing: content-box`/`border-box` 改变指定宽高与 min/max 约束的解释方式；
  `overflow: hidden` 把盒的绘制后代裁剪到 padding box；`text-decoration` 现在除 `underline` 外还绘制
  `line-through` 与 `overline`。
- `style`：新的计算类型 `BorderSide`（px 宽度、`BorderStyle`、可选颜色，nullopt 表示
  `currentColor`）、`BoxSizing`、`Overflow` 与 `UsedBorderWidth`/`UsedBorderColor` 辅助函数。解析的
  属性：`border-*-width`（px、thin/medium/thick = 1/3/5px）、`border-*-style`（全部十种关键字；
  none/hidden 的 used width 为 0）、`border-*-color`（M14a 颜色集，含 alpha/transparent/
  currentColor）、1–4 值的 `border-width/style/color` 简写、`border-top/right/bottom/left` 与
  `border` 简写（CSS 重置到初始值语义）、`box-sizing`、`overflow` 简写 + `overflow-x/y`，以及
  三标志的 `text-decoration` 重置语义。border/box-sizing/overflow 不继承；装饰标志继承。样式 dump
  保留所有 M14b 之前字段，`text-decoration=` 扩为空格连接 token，并在行尾追加
  ` border-*-width= border-*-style= border-color= box-sizing= overflow=`。
- `layout`：used border 宽度进入盒模型（border box = content + padding + borders，content box 由
  border + padding 内缩）。`content-box`（默认）保持宽高为内容尺寸（M14a 行为）；`border-box` 使宽高为
  border-box 尺寸（content = clamp(size - padding - borders, >=0)）。px min/max-width/height 按
  box-sizing 解释，auto 宽度填充 包含宽度 - margin - padding - border，auto margin 按最终 border box
  分配剩余空间。行内元素的水平 border 加入左右 advance 与 fragment 宽度（fragment 首尾各含一侧；
  行内垂直 border 与 replaced 元素的 border/padding/margin 仍按文档化限制忽略）。overflow 与装饰不改变
  布局。
- `paint`：元素绘制顺序为 background → borders → image → children/text。border 从 border-box 矩形按
  top/right/bottom/left 顺序发出最多四条设备像素 `FillRect`，颜色用解析后的 `UsedBorderColor`
  （currentColor → 文本色，保留 alpha）；本 MVP 中所有可见样式都按实心绘制，每条矩形按背景同样的规则
  做视口裁剪。`overflow:hidden` 的盒在自身背景/边框/图片之后压入其 padding-box clip，并在整棵子树之后
  弹出（空子树也成对、嵌套正确；裁剪只影响绘制，滚动内容高度仍包含溢出盒）。文本装饰在文字命令后按
  underline → line-through → overline 顺序发出 1px `FillRect`（underline：baseline+1；
  line-through：baseline - max(1, round(font_size/3))；overline：盒顶部），使用不透明文本色与相同
  文本裁剪，隐藏文本不绘制。
- 测试/fixtures：`tests/fixtures/css_compat_site/` 新增 `borders.html`（1–4 边、不同宽度/样式/
  颜色/currentColor/rgba、border 简写、content-box vs border-box、嵌套 border 盒）、
  `overflow.html`（被裁剪的超大子元素 + 文本、visible 对照、带 border 的裁剪、后续盒）与
  `decorations.html`（三种装饰各自/组合/继承/重置）；README 记录每个期望计算值、盒矩形、装饰行与
  像素，`legacy.html` 保持与 M14a 逐字节相同。新增 `style.borders`、`style.box_model`、
  `layout.box_model`，扩展 paint 显示列表测试与 `shell.css_compat`（另加 CLI
  `shell.dump_style_css_compat_borders` 与 `shell.css_compat_screenshot_borders`）；共 85 个测试。
- 已达成验收：所有 80 个 M14b 之前测试加 5 个新测试在 GCC 与 Clang 下、警告视为错误时通过（共 85 个
  测试）；M14b 之前的页面保持逐字节相同的盒 dump、截图与退出码（对 M14a 二进制的 31 用例门禁，
  style dump 只对追加字段规范化）；与 M14（M13）更早二进制的对比只在 M14a fixture 页面上有差异；
  GCC/Clang 输出相同；手工真实站点检查显示本批次生效：python.org 现在绘制导航项的彩色 border
  （148 个元素有 used border），其通用 `*{box-sizing:border-box}` 使 836 个元素重排；w3.org 的
  “Skip to content” 得到黑色 border 盒且 10 个 overflow-hidden 元素裁剪；gnu.org 的 skip 链接绘制
  其 border；三站 M14b 之前的 style 字段与基线完全一致。没有新依赖。
- M14b 之后的已知限制：所有 border 样式都按实心线绘制（dashed/dotted/double/groove/ridge/inset/
  outset 参与盒模型但不渲染图案）；百分比 border 宽度被忽略；`overflow` 只裁剪绘制（没有滚动条、没有
  水平滚动，内容高度仍包含溢出盒），且 `overflow-x/y: hidden` 会同时裁剪两轴（MVP 简化）；行内垂直
  border 与 replaced 元素的 border/padding/margin 仍被忽略；text-decoration 的样式/颜色组件被忽略
  （装饰使用文本色的 1px 行）；`border-radius`、`box-shadow`、position/float/flex/grid 与其他
  display 值仍在后续轮次。同一 CSS 调查中剩余的真实站点差距：position/float 219 条声明、
  border-radius 121、box-shadow 129、display inline-block/flex/grid/table 132、transform/animation
  105。

## M15 — Flexbox / position / float 子集（计划中；下一个里程碑）

- 目标：支持现代静态页面最常用的布局机制：flexbox 的实用子集（row/column 方向、`justify-content`、
  `align-items`、`gap`、`flex-grow`/`shrink`/`basis`、换行）和/或基础 `position`/`float`。具体拆分由
  M14 的实测决定；flexbox 本身足够大，可以单独成为一个垂直切片。
- 验收：支持子集的 fixture 页面加真实站点视觉检查；确定性盒 dump 与截图；无回归。
- 在重新划范围之前不做：grid、表格、表单、动画/transform、完整规范符合性。

## M16 — HTTP 代理（计划中；用户要求 M9 之后延后）

- `http://` 通过 HTTP 代理以 absolute-form 请求目标抓取；`https://` 通过代理 `CONNECT` 隧道后再做
  正常的 TLS 握手。
- CLI `--proxy host:port` 加标准 `http_proxy`/`https_proxy` 环境变量回退；进程内假代理测试。这是在
  直接出网受限的网络上让真实站点测试变得实用的能力。
- 范围与契约在该轮开始时冻结；预计不需要新依赖。

## M17 — JPEG 图片支持 ✅（已完成；应维护者要求在顺序之外交付）

- 目标已达成：JPEG/JPG 的 `<img>` 源现在能像 PNG 一样解码并渲染，混合格式页面也可用；不再因为
  文件是 JPEG 而拒绝，而是使用用户批准的 libjpeg（libjpeg-turbo）依赖。
- 依赖：用户于 2026-10-31 批准 libjpeg-turbo 用于 JPEG 解码，记录在
  [dependencies.md](dependencies.md)（清单/审批记录/候选表）；通过 `find_package(JPEG REQUIRED)`
  发现并 PRIVATE 链接到 `toy_render`（无 vendoring、不硬编码版本/路径）；README/CONTRIBUTING 的
  安装包列表与 CI apt 列表都加入了 `libjpeg-dev`。
- `render`：新增 `render::DecodeJpeg`（libjpeg 类型保持私有）与按内容的 `render::DecodeImage`，
  与未改动的 `render::DecodePng` 并列。`DecodeImage` 用八字节 PNG 签名与 JPEG 的 `FF D8 FF`
  SOI 标记识别格式——绝不看扩展名——再分派给对应解码器；未知数据返回可读错误。`DecodeJpeg` 支持
  baseline 与 progressive，以及灰度、RGB、YCbCr、CMYK/YCCK 源，输出始终是 straight-alpha ARGB
  （alpha 255）。它在分配前强制与 PNG 相同的包含边界（每边 `kMaxImageDimension`、总计
  `kMaxImagePixels`），不抛异常，失败时保持 `out` 不变。libjpeg-turbo 的 libjpeg API 无法把
  CMYK/YCCK 转换成 RGB（`JERR_CONVERSION_NOTIMPL`），因此这些流解码为 `JCS_CMYK`，再用标准的
  Adobe 反相公式 `channel = sample * black / 255` 折叠。EXIF 方向、ICC 配置与其他元数据被忽略
  （按存储方向解码）。截断警告（`Premature end of JPEG file`、
  `Corrupt JPEG data: premature end of data segment`）提升为解码错误，partial 图片绝不会被当作
  完整图片返回；良性警告（例如 marker 前多余字节）被静默容忍，且不打印任何内容。第一版实现中的
  崩溃（libjpeg 错误管理器从错误的 struct 偏移恢复，导致 `longjmp` 使用损坏上下文）由 shell 任务
  发现并修复：改用 standard-layout 的 `ErrorManager { jpeg_error_base; context* }` 加
  `offsetof`/standard-layout static_assert，并加入截断输入回归测试。
- `shell`：私有图片加载器在原来的调用点改用 `DecodeImage` 而不是 `DecodePng`，因此页面可以混用
  PNG 与 JPEG 源；失败仍被静默跳过并按解析后的 URL 记忆，图片失败绝不让页面加载失败，M13 的页内
  URL 去重/缓存语义不变。
- Fixtures/测试：提交自造的 JPEG fixture 与基于 libjpeg 的生成器
  `tests/fixtures/images/generate_jpeg.cpp`：baseline RGB 四象限、灰度、progressive、Adobe CMYK、
  9000x1 超宽用例与截断流；fixture README 记录像素内容、每个 fixture 的容差（JPEG 有损）、CMYK/
  警告策略与重新生成命令（不同 libjpeg 构建解码电平可能相差一两级；测试按容差比较，绝不比较整
  文件）。新增 `render.jpeg` 覆盖解码、边界、错误时 `out` 不变、`DecodeImage` 分派、非法/垃圾数据
  与确定性；`tests/fixtures/jpeg_site/` 加 `shell.jpeg`/`shell.jpeg_dump_boxes` 覆盖混合 PNG/JPEG
  页面的固有尺寸/CSS 缩放、缺失/截断静默跳过以及容差内的 Browser 像素；共 88 个测试。
- 已达成验收：所有 85 个 M17 之前测试加 3 个新测试在 GCC 与 Clang 下、警告视为错误时通过（88 个
  测试）；所有 M17 之前输出（PNG 图片页、CSS 页、dump、截图、退出码）与 pre-JPEG 二进制逐字节
  相同（25 用例门禁，无需规范化），GCC/Clang 输出一致；可直接访问的真实站点检查显示 JPEG 生效：
  gnu.org 的 `<img>` 盒 8 -> 13，五张发行版截图 JPEG 现在被绘制（800x1600 像素差 7.98%），
  debian.org 盒 3 -> 8、五张 JPEG 照片（5.79%）；w3.org 的三张 JPEG 能解码但不可见，因为它们是
  `display:block` replaced 元素——既有 M8a 布局限制（M15 范围）。除已批准的 libjpeg 外没有新运行时
  依赖。
- M17 之后的已知限制：JPEG 元数据被忽略（无 EXIF 方向、无 ICC/色彩管理）；CMYK 解码假定标准
  Adobe 反相样本（CMYK JPEG 的标准；非 Adobe 四分量文件罕见且同样处理）；解码通道有损且可能随
  libjpeg 构建略有差异（测试使用容差）；GIF/WebP/SVG、`background-image`/`srcset` 仍不支持；既有
  `display:block` replaced 元素布局限制在 M15 之前仍会隐藏部分真实站点 JPEG。

## 暂缓 —— JavaScript 子集

- 用户于 2026-10-31 暂缓 JavaScript 子集；它不是下一个里程碑。它仍是一个大的可选里程碑（词法
  分析器、解析器、小型解释器、DOM 绑定与事件处理器）；没有用户明确的优先级决定不要开始。

## M18+ — 机会性工作

更多 CSS（补充 flex/grid、表格）、表单、更多图片格式、更好的文本处理、一致性测试套件与进一步性能。每一项都只在前一个切片干净、且维护者同意优先级之后才开始。

## Backlog — 计划中的后续工作

- **更多图片格式与背景图**——GIF/WebP/SVG、`background-image`、`srcset`、图片缓存与懒加载；JPEG 已在 M17 交付，其余按需排期，不阻塞任何里程碑。
- **Web 字体与更丰富的文本**——`@font-face`/字体下载、合成粗体/斜体、可变字体实例、bidi/RTL 与完整 Unicode 断行（UAX#14）、泰语/高棉语/老挝语词典断行与复杂脚本细化；M10 刻意先覆盖英文与中文，其他语言通过 fontconfig 字符覆盖尽力支持。
- **测试辅助代码合并**——较旧的测试可执行文件里仍然重复着每个文件各自的 `Check`/`g_failures` 测试支架。header-only 的 `tests/support/check.h`（接口目标 `toy_test_support`）已在 M5 引入，新测试都在使用；旧文件只在被改动时迁移。不引入框架依赖。

## 规则

- 前一个里程碑的演示可用之前，不要开始下一个里程碑。
- 绝不让里程碑因等待依赖审批而阻塞：先交付 headless 或自实现切片，审批通过后再升级。
- 任何里程碑都可以被用户重新划定范围。MVP 永远优先于完整性。
- headless 优先：任何可见或可观察的结果都必须能在不开窗口的情况下检查（dump 命令或截图），这样项目可以完全 headless 地开发和测试。
