> 本文为中文翻译，英文原版为准：[architecture.md](../architecture.md)。

# 架构

面向 MVP 的设计：让每个里程碑跑通的最薄实现。这里没有任何推测性内容——抽象只在真实切片需要时才加入。

## 模块划分

| 模块 | 职责 | 可依赖 | 首次需要 |
| --- | --- | --- | --- |
| `base` | 小型共享工具（几何、日志、UTF-8），只在出现需要时才存在 | — | 首次需要时 |
| `platform` | 窗口、输入事件、事件循环、呈现像素表面 | `base` | M1 |
| `render` | 显示列表、软件光栅化器、内置位图字体（度量与字形）、PNG + JPEG 图片解码（已批准的 libpng/libjpeg）、真实字体引擎（已批准的 freetype + harfbuzz） | `base` | M1 |
| `net` | URL 解析、socket、HTTP/1.1 抓取（纯 TCP 或经校验的 TLS，已批准 OpenSSL）、keep-alive `Client` 与 gzip（已批准 zlib）、`file://` 读取 | `base` | M2 |
| `dom` | HTML tokenizer（词法分析器）、树构建器、DOM arena（区域分配器） | `base` | M3 |
| `css` | CSS tokenizer、规则/声明解析器、选择器 | `base` | M4 |
| `style` | 层叠与计算样式、默认样式表；作者 `<style>` 规则加调用方提供的按文档顺序的外部链接样式表（M12） | `dom`、`css` | M4 |
| `layout` | 盒树、块级/行内布局、换行（词单元加 CJK 断行单元） | `dom`、`style`、来自 `shell` 的文本度量回调 | M5 |
| `paint` | 从盒树构建显示列表（元素背景与文本） | `dom`、`style`、`layout`、`render` | M6 |
| `js` | 小型 JavaScript 引擎与 DOM 绑定（可选，用户已暂缓） | `dom`、`base` | 暂缓 |
| `shell` | 导航、流水线编排、窗口外壳、字体选择（字体族查找使用已批准的 fontconfig） | 所有模块 | M1 |

依赖只向下指向；没有环。`render` 完全不了解 HTML/CSS（它消费显示列表）；`paint` 认识 DOM/样式/布局数据但从不光栅化；`platform` 不了解页面内容（它 blit 像素并上报输入）。只有 `shell` 能看到所有模块并把它们接在一起。`base` 尚不存在：M1–M6 的模块只用标准库与标准 POSIX 接口，只有当共享工具确实需要时该模块才出现。

新模块注册在根与 `tests` `CMakeLists.txt` 的显式列表中；Lead 添加一次名字。`--dump-*` 模式与退出码（0 成功，1 运行时错误，2 用法错误）是冻结接口。

## 一次页面加载的完整流程

1. `shell` 收到 URL（CLI 参数或链接点击）。
2. `net` 通过本次页面加载的 `net::Client` 抓取它：字节（gzip 已解码）、最终 URL、状态；Client 为后续
   资源复用 keep-alive 连接（M13）。
3. 字节被解码成 UTF-8（MVP：假定 UTF-8，剥掉 BOM；其他编码以后再说）。
4. `dom` 把 HTML 解析成 `Document`。
5. `shell` 发现页面的作者样式表：文档自己的 `<style>` 元素（由 `css` 解析）以及每个适用的
   `<link rel="stylesheet">`，其 `href` 相对页面最终 URL 解析、经同一个 Client 抓取，失败时静默
   跳过（M12/M13）。
6. `style` 把层叠解析成样式树：先 UA 规则，再按文档顺序应用作者 `<style>` 与链接样式表，最后是
   内联 `style` 声明。
7. `shell` 遍历 DOM 中的 `<img>` 元素，对每个首个 `src` 相对页面最终 URL 解析、经同一个 Client 抓取，
   并用 `render::DecodePng` 解码；失败被跳过，页内 URL 缓存对重复图片 URL 去重（M8a/M13）。
8. `layout` 把样式树变成带几何信息的盒树，replaced 元素使用 shell 提供的固有尺寸。
9. `paint` 从盒树构建扁平显示列表（背景、图片、文本），`render` 把它光栅化成 `Surface`。
10. `platform` 呈现该表面（窗口 blit，或在 headless 模式下输出 PPM dump）。

MVP 保持整条链路同步、单线程。每个阶段接收普通数据并返回普通数据，因此每个阶段都能在测试中通过 `--dump-*` 命令单独驱动。

网络（M2，TLS 在 M9）：`net` 拥有 URL、socket 和 HTTP。抓取是阻塞式 HTTP/1.1 GET——每个连接一个请求、连接与读取超时、`Content-Length` 或 chunked body、重定向（M9 起用 `net::ResolveUrl` 解析，因此相对与协议相对 `Location` 可用）与 `Connection: close`——封装在 `Fetch()` 之后，因此以后可以改成非阻塞而不改调用方。`net` 是唯一允许触碰 socket 的模块；页面代码永远看不到 socket。代理支持（先支持 HTTP 代理）计划为 M16，放在 `FetchOptions` 后面并保持在 `net` 内部；它不需要新依赖。

TLS / HTTPS（M9）：`net` 接受 `https` URL（默认端口 443），通过已批准的 OpenSSL 库在保持不变的阻塞 `Fetch()` 之后抓取。纯 TCP 与 TLS 共享同一份 HTTP/1.1 实现：`src/net/http.cpp` 在一个私有的 `internal::Stream` 接口（“写入全部字节” + “读若干字节直到 EOF”）之上构建请求、解析响应，接口由纯 `SocketStream` 与 `TlsStream` 实现；传输层按 URL scheme 在一处选择（`src/net/stream_internal.h`、`socket_internal.h`、`tls_internal.h` 均为私有，公开头文件中没有 OpenSSL 类型）。进程级 `SSL_CTX` 在第一次 https 抓取时从 OpenSSL 默认校验路径惰性创建（标准 `SSL_CERT_FILE`/`SSL_CERT_DIR` 覆盖在该时刻生效），`SSL_VERIFY_PEER` 始终开启，DNS 主机每连接发送 SNI，证书链 + 主机名校验（DNS 名用 `SSL_set1_host`，IPv4 字面量用 IP SAN 校验）。没有 insecure 模式。TCP 连接用 `connect_timeout_ms`；TLS 握手与所有 TLS I/O 用 `read_timeout_ms`，运行在现有非阻塞 socket + `poll` 循环上；超时、证书与主机名失败都是普通可读的 `Fetch` 错误。干净的 `close_notify` 与非干净 EOF 都结束流（截断仍由 Content-Length/chunked 捕获），TLS 路径忽略一次 `SIGPIPE`，坏掉的对端无法杀死浏览器。未实现 ALPN/HTTP/2、cookie、HSTS、客户端证书、TLS 会话复用或 HTTP 缓存；由于 shell 已经全部经过 `ParseUrl`/`ResolveUrl`/`Fetch`，M9 无需改动其 API 或 CLI（后来页面加载改用 M13 的 `Client`，它复用同样的 URL 与 TLS 机制）。

资源加载（M13）：`net::Client`（`toy/net/http.h`，pimpl）是 shell 在页面加载时使用的 keep-alive HTTP/1.1 路径。它每个 origin（scheme/host/port）最多保留一条空闲连接，发送 `Connection: keep-alive` 与 `Accept-Encoding: gzip`，并在私有 `Stream` 上增量解析每个响应：有界读取 header，然后精确读取 Content-Length body 或完整 chunked body（含 trailer），读到的多余字节留在池化连接上供下一请求使用。持久连接遵循 HTTP/1.1 规则（无 `Connection: close`）或 HTTP/1.0（`Connection: keep-alive`）并要求长度定界；持久响应缺少长度定界是可读错误。gzip body 用已批准的 zlib 解压（identity 直接通过，其他编码是错误，原始与解码后 body 都受 `max_body_bytes` 限制，header 原样保留）。池化连接在收到任何响应字节前失败时会在新连接上重试一次；超时与新连接失败不重试。重定向与 `file://` 行为与 `Fetch()` 一致，而一次性 `Fetch()` 本身不变（请求字节保持 `Connection: close`、不协商压缩），仍是不需要池化的调用方的便捷 API。`shell` 每次 `LoadPage` 创建一个 `Client`，用于页面、其样式表与图片，并在本次加载内按解析后的 URL 去重图片（重复 `<img>` 共享一次抓取、解码与 `render::Image` 对象；失败 URL 被记住，重复项不会重试；无跨加载缓存）。

HTML 与 DOM（M3）：`dom` 拥有 tokenizer、简化树构建器和 arena DOM。解析是容错的，且总是产生一个带 `head` 和 `body` 子节点的 `html` 元素；tokenizer 覆盖支持的子集（元素与属性、注释、doctype、raw-text 与 RCDATA 元素、少量实体），遇到畸形输入也永不失败。`DumpDocument()` 是确定性的文本形式，供 `--dump-dom` 与解析器测试使用。节点用整数 id 寻址（没有持有指针、没有环）；DOM 完全不了解 CSS、布局或脚本。

样式（M4）：`css` 把样式表与声明列表解析成普通结构，完全不了解 DOM。`style` 在 DOM 上匹配这些选择器，应用 UA/作者/行内层叠——依据 specificity（优先级）、`!important` 与继承——并为每个元素生成一个 `ComputedStyle`；内置的 UA 样式表本身就是由我们的解析器解析的 CSS。`DumpStyleTree()` 是 `--dump-style` 与样式测试背后的确定性文本形式。布局只消费样式树，永远不会消费原始 CSS。自 M12 起作者层叠还包含 shell 提供的外部链接样式表：`style::LinkedStyleSheet` 指定一个 `<link>` DOM 节点与抓取到的 CSS 文本，双参数 `ComputeStyles(document, linked_sheets)` 把每个样式表插到该元素的文档位置（一次前序遍历把它与文档自己的 `<style>` 元素交错）。`style` 保持无 I/O、无网络代码；单参数重载精确保留 M12 之前的行为。

外部样式表（M12）：`shell` 的私有 `LoadStyledDocument()`（位于 `src/shell/page.h`）拥有全部资源策略：抓取并解析页面后，它按文档顺序遍历一次 DOM，过滤 `<link>` 元素（`rel` token 含 `stylesheet` 且不含 `alternate`、无 `disabled` 属性、`media` 为空/`all`/`screen`、`type` 为空/`text/css`），用 `net::ResolveUrl` 把每个 `href` 相对重定向后的最终页面 URL 解析，用与页面相同的 `net::Fetch` 抓取（默认超时与 16 MiB body 上限），只接受 200..299 的 body，每个适用出现抓取一次（不去重、无缓存，和图片一样），并静默跳过所有被过滤、失败或非 2xx 的样式表。得到的 `style::LinkedStyleSheet` 列表交给 `style::ComputeStyles`，同一条路径由正常页面加载（窗口、`--screenshot`、`--dump-boxes`）与 `--dump-style` 共享；`--dump-dom` 与 `--dump-body` 仍不抓取样式表。只有页面抓取可以让加载失败。媒体查询、`@import`、alternate/disabled 样式表、带参数的 `type` 值与 CSS 图片在本轮刻意不实现。

CSS 值（M14a）：`ComputedStyle` 携带 M14a 值模型：盒长度 `Length {value, unit: px|percent}`（盒属性的百分比保持未解析交给 layout）、`AutoLength` margin、`LineHeight {kind: normal|number|px, value}`、`TextAlign` 与 `Visibility`。style 在存储前解析所有字体相对单位：`em` 相对元素自身最终字号、`%` 字号相对父元素、`rem` 相对根 `<html>` 元素的计算字号（根元素自身的 `rem` 用 16px 初始值）。颜色解析 `#rgb`/`#rgba`/`#rrggbb`/`#rrggbbaa`、旧式逗号分隔 `rgb()`/`rgba()`/`hsl()`/`hsla()`、完整 CSS Color 4 命名颜色集与 `transparent`；文本颜色保持不透明（丢弃 alpha），背景色保留 alpha。`background` 简写只应用颜色部分。逻辑属性（margin/padding 的 block/inline 长写与简写、`inline-size`/`block-size`、min/max 变体）在 horizontal-tb 下映射到物理字段。`line-height`、`text-align`、`visibility` 与 `text-decoration: underline` 按 MVP 契约继承（text-decoration 传播简化为继承，只表示 underline 标志）。不支持的写法（百分比高度、line-height 的 rem、`justify` 只按 left、空格分隔颜色函数语法、`pt`/`vh`/`vw` 单位）被忽略或作为已知限制记录。`DumpStyleTree()` 保留所有 M14 之前字段并在行尾追加 `min-width/min-height/max-width/max-height/line-height/text-align/visibility/text-decoration`；自 M14b 起 `ComputedStyle` 还携带 `BorderSide {Length width; BorderStyle style; optional<uint32_t> color}`（颜色 nullopt = currentColor，`UsedBorderWidth` 对 none/hidden 为 0）、`BoxSizing` 与 `Overflow`，并在 M14a 字段之后追加 `border-*-width/border-*-style/border-color/box-sizing/overflow`。本 MVP 中所有可见 border 样式都按实心线绘制；border 宽度不接受百分比。`line-height`/`text-align`/`visibility`/三个 `text-decoration` 标志按 MVP 契约继承（underline/line-through/overline 自 M14b 起都已表示并绘制）。

M14a 布局：`Length::kPercent` 盒长度相对包含块内容宽度解析（width、min/max-width 与四边 margin/padding——CSS 对垂直百分比也用宽度）；百分比高度不支持、按 auto 处理，只有 px 的 min/max-height 在内容/固定高度之后钳制（先 min 后 max）。内容宽度在指定/auto 宽度解析后按 min 后 max 钳制；宽度确定（指定或经钳制解析，这正是常见 `max-width + margin: auto` 包裹盒居中的条件）时水平 `auto` margin 分配剩余空间，auto 宽度与过约束盒保持 0。文本项的 line-height 贡献为计算 px 值、数字×字号或 `normal` 的测量行高；行盒取最大贡献，half-leading 把共享基线定位到 `baseline_offset_px`（`normal` 精确贡献测量行高，因此 M14 之前几何不变）。断行后 `text-align: center/right` 整体平移已定稿行的盒（`justify` 按 left；过长行保持左对齐）。行内/replaced 的百分比 margin/padding 用行宽，replaced 百分比宽度用包含宽度、百分比高度保持 auto。visibility 不影响布局。

M14a 绘制：`visibility` 为 hidden 的盒不输出背景、图片、文本或下划线，但遍历仍进入子节点，因此可见后代正常绘制；布局保留隐藏盒的空间。`text-decoration: underline` 的文本盒在其文本命令之后发出一个设备像素 `FillRect`，x=round(box.x)、y=round(box.y)-scroll_y+round(baseline_offset_px)+1、width=round(box.width)、height=1、颜色为不透明文本色，使用相同的文本裁剪。不使用这两个属性的页面构建与 M14 之前逐字节相同的命令列表。位图路径仍在行盒内顶部对齐字形，因此大 CSS line-height 会显示在字形下方多出空间（real-font 路径通过 half-leading 基线居中）；这是有文档的位图模式简化。

M14b 布局：used border 宽度（`UsedBorderWidth`）进入盒模型：border box = content + padding + 四边
borders，content box = border box 内缩 border + padding。`box-sizing: content-box`（默认）下宽高仍是
内容尺寸，auto 宽度填充 `包含宽度 - margin - padding - border`；`border-box` 下宽高是 border-box
尺寸，content = clamp(size - padding - border, >=0)，auto 宽度仍然填充。px min/max-width/height 按
box-sizing 解释（content-box 钳制内容盒，border-box 钳制 border box，非负，先 min 后 max）；百分比
尺寸/约束仍用包含块内容宽度。auto margin 分配 `包含宽度 - margin - 最终 border box 宽度`（CSS 10.4
居中，过约束为 0）。固定/auto 高度遵循同样规则（百分比高度仍不支持）。行内元素把水平 border 加入
左右 advance 与 fragment 宽度（首 fragment 含 border-left，末 fragment 含 border-right；行内垂直
border 仍忽略），replaced 行内元素按 M8a 限制忽略 border/padding/margin，overflow/装饰从不改变布局。
没有 border/box-sizing 变化的页面保持 M14b 之前几何逐字节不变。

M14b 绘制：元素绘制顺序为 background -> borders -> image -> children/text。border 从 border-box
矩形按 top/right/bottom/left 顺序发出最多四条 `FillRect`，颜色用解析后的 `UsedBorderColor`
（currentColor -> 文本色，保留 alpha），每条按背景同样规则裁剪；所有解析到的 border 样式都按实心
绘制，隐藏盒不绘制 border。`overflow:hidden`（任一轴）的盒在整棵子树周围发出一个成对的
`PushClip`/`PopClip`，裁剪到其 padding box（整数取整边界；空子树也成对、嵌套正确）；裁剪只影响
绘制，因此 `ContentHeight` 仍包含溢出盒。文本装饰在文本命令之后按 underline -> line-through ->
overline 顺序发出：underline 在 baseline+1，line-through 在 baseline - max(1,
round(font_size/3))，overline 在盒顶部，各为 1px `FillRect`、不透明文本色，使用相同文本裁剪且隐藏
文本不绘制。不使用新属性的页面构建与 M14b 之前逐字节相同的命令列表。

布局（M5）：`layout` 把样式树转换成带绝对 CSS 像素几何的 `BoxTree`。换行需要文本度量，因此 `LayoutDocument()` 接受一个由 `shell` 提供的 `MeasureText` 回调（只用标准类型加 `style::ComputedStyle`）；内置等宽位图字体度量位于 `render`，由 `shell` 适配——若为真实字体，则适配该字体的整形 advance、行高与 ascent（M8b/M10），布局本身仍然完全不了解字体。这样保持依赖方向不变：没有 `layout` → `render` 的边，`layout` 中也不重复字体代码。M10 起回调会收到文本的计算样式，因此 shell 能按样式选字体；`TextMetrics` 携带 ascent，布局把该行最大的 ascent 写进每个文本盒的 `Box::baseline_offset_px`（同一行不同字体因此在绘制时共享基线）。文本被切成断行单元（`src/layout/text_break.cpp`）：空白分隔单词，非 CJK 文本保持最长词，CJK 字符各自成为断行机会并带简化的标点禁则，因此没有空格的中文也能正常换行。每个单元仍是普通的可度量 word 级文本盒，复用 M5 的适配、行高与基线机制。`DumpBoxTree()` 是 `--dump-boxes` 与布局测试背后的确定性文本形式。布局只消费样式树：它从不读取原始 CSS，也从不绘制。几何是输入的纯函数；MVP 简化的摘要见 [roadmap.md](roadmap.md) 的 known-limits 列表。M8a 把 replaced 元素加入同一套布局：`LayoutOptions::replaced_sizes` 把 DOM 节点 id 映射到 shell 提供的固有 CSS 像素尺寸；有条目的行内元素成为原子行内级盒（CSS width/height px 覆盖、只给一侧时保持固有宽高比、整体换行、顶部对齐、抬高行高），而 `layout` 仍然从不读取图片、也不依赖 `render`。M10 的断行单元模型是对 Unicode 断行的简化（没有完整 UAX#14、没有泰语/高棉语/老挝语词典断行、没有连字符断行、不做跨文本节点标点粘连）；拉丁词永不拆开。

绘制（M6）：`paint` 按前序遍历盒树，产出扁平的 `render::DisplayList`：每个计算背景非透明的元素盒变成一条 `FillRect`（背景先于子内容），每个节点 id 有解码图片的行内 replaced 盒变成一条 `DrawImage`（M8a，在背景之后、子内容之前），每个文本盒变成一条或多条 `DrawTextWithFont` run，或（位图模式）一条 `DrawText`，都使用继承的颜色与字号。M10 起 paint 从 shell 接收 `paint::FontRunsProvider` 而不是单一字体：provider 返回该盒文本的有序真实字体 run，绘制器按每个 run 的整形宽度推进 pen，并把所有 run 放在 `box.baseline_offset_px` 上；空 provider（或某盒返回空 run）逐字节复现 M10 之前的位图命令列表。`paint` 从不光栅化，也从不包含 render 的实现头文件。`render` 执行该列表：矩形填充对不透明目标使用冻结的 SrcOver 整数公式；文本使用内置 8x16 ASCII 字形表，按最近邻单元缩放（无抗锯齿）；图片使用冻结的最近邻源映射（见下）；整数矩形裁剪栈约束每条命令（M7 会用视口 `PushClip`/`PopClip` 对包裹页面，并把每条命令按滚动偏移平移）。`shell` 把 canvas 填白、按 surface 宽度布局页面、构建列表并光栅化；窗口与 `--screenshot` 共享完全相同的这条路径。确定性正是重点：相同输入在 GCC 和 Clang 上产出逐字节相同的 PPM，截图测试依赖这一点。

图片（M8a，M17 扩展）：`render` 在 `render::DecodePng()`、`render::DecodeJpeg()` 与按内容的
`render::DecodeImage()` 之后拥有图片解码：已批准的 libpng 与 libjpeg（libjpeg-turbo）依赖分别封装在
`src/render/image.cpp` 与 `src/render/jpeg.cpp` 中，像素转换成直通 alpha 的 ARGB `render::Image`，
尺寸与像素数在分配前校验，公开头文件里不出现任何 libpng/libjpeg 类型。`DecodeImage` 用八字节 PNG
签名与 JPEG 的 `FF D8 FF` SOI 标记识别格式（绝不看扩展名）并分派，调用方因此与编解码器解耦。JPEG
支持 baseline 与 progressive，以及灰度/RGB/YCbCr/CMYK-YCCK 源（alpha 始终 255）：libjpeg 的 libjpeg
API 无法把 CMYK 转成 RGB，因此 CMYK/YCCK 解码为 `JCS_CMYK`，再用 Adobe 反相公式
`channel = sample*black/255` 折叠；EXIF 方向与 ICC 元数据被忽略（按存储方向解码）；libjpeg 的截断
警告变成解码错误，良性警告被静默容忍（不打印任何内容）。`shell` 通过私有加载器（`src/shell/page.h`）拥有页面级策略：
在抓取、解析与样式之后、布局之前，把每个 `<img src>` 相对页面最终 URL 解析，经本次页面加载的
`net::Client` 抓取（http/https/file；M13 之前用 `net::Fetch`），解码，并维护按 img DOM 节点 id 索引的
`paint::ImageMap` 与 `layout::ReplacedSizes` 映射。M13 起加载器在单次页面加载内按 URL 去重图片：
重复的 `<img src>` 共享一次抓取、一个解码后的图片对象与一个固有尺寸；失败的抓取/解码被记住，重复的
坏图片不会重试（仍无跨加载缓存）。M17 起加载器调用 `render::DecodeImage`，因此一页可以混用 PNG
与 JPEG 的 `<img>` 源，无需扩展名检查；PNG 与 JPEG 失败共享相同的静默跳过策略。缺失或失败的图片
被静默跳过，元素按空行内布局（MVP 限制）。
`paint` 把有解码图片的行内 replaced 盒变成 `DrawImage` 命令；显示列表持有
`shared_ptr<const Image>`，因此列表可以比构建它的页面数据活得更久。`render` 用冻结的最近邻整数
源映射、与填充相同的 SrcOver 公式和裁剪栈执行图片。`shell::Browser` 随页面保存图片并绘制它们，
resize 时重新布局而不重新抓取；`--dump-boxes` 使用同一加载器，因此图片几何出现在 dump 中，而
`--dump-dom` 保持无图片、`--dump-style` 应用外部样式表但不抓取图片。

真实字体（M8b）：`render::Font` 在 pimpl 之后封装已批准的 freetype 与 harfbuzz 依赖：公开头文件不出现
freetype/harfbuzz 类型，加载失败是普通错误，字体自己持有 face 字节与 freetype library。度量来自
字体设计单位（`ascent = size * ascender / unitsPerEm`，行高来自 `face->height`），advance 宽度
在请求字号下由 harfbuzz 整形（26.6 求和），`internal::FontRasterizer`（render 私有）把 freetype
8-bit 覆盖率位图按冻结的逐字形整数放置公式画到显示列表基线上，用 SrcOver 覆盖率混合，并裁剪到
当前 clip。有效字号被钳制到 `kMaxRealFontSizePx`（4096 px），对抗性 CSS 无法请求无界字形位图。
M10 起 `paint` 为每个真实字体 run 发出一条 `DrawTextWithFont`（放在布局提供的基线上），provider
返回空时发出冻结的位图 `DrawText`；显示列表和图片一样按共享引用持有字体。M8b 的“每次运行一个
face”（`--font PATH` 或 fontconfig 字体族匹配）已被 M10 的按样式自动选择取代（下一段）。fontconfig
从不离开 shell，字体也从不离开 render 的私有实现。

字体选择（M10）：`shell` 的公开 `Fonts` 对象（`toy/shell/fonts.h`，pimpl）把计算出的文本样式与
页面语言变成具体 face。对每个不同的（font-family 列表、weight、slant、language）组合构造
fontconfig pattern（每个字体族一个 `FC_FAMILY`，`FC_WEIGHT` 经 `FcWeightFromOpenType`，`FC_SLANT`，
根 `<html lang>` 非空时加 `FC_LANG`），执行标准的 substitute/default/sort 流程，并缓存带字符集的
有序候选 face；已加载的 `render::Font` 按文件路径缓存。文本盒按码点切成 run：每个码点交给第一个
字符集覆盖它的候选（因此拉丁页面上的汉字回退到已安装汉字 face，反之亦然），同一 face 的连续码点
合成一个 run；`MeasureText` 累加 run advance，`Runs` 把同样的 run 交给 paint——度量与绘制因此天然
一致。没有可用 face 时（fontconfig 失败、无匹配、无可加载文件）对象进入位图模式：用内置位图度量，
`Runs` 返回空，浏览器仍可用，M10 之前的 golden 输出仍可复现。`--font PATH` 强制单一精确 face
（无回退）；`--font-family NAME` 强制首选字体族（仍保留字符回退），并开始遵循计算出的 weight/style。
初始字体族为 `serif`；其他语言通过同一套字符覆盖机制尽力支持（暂无 bidi/RTL 重排与词典断行）。

性能（M11）：浏览器在不改变任何渲染像素的前提下优化交互路径。未指定构建类型时默认使用
`RelWithDebInfo`，第一方目标编译时加 `-ffp-contract=off`，使布局几何在不同构建类型与编译器间
逐字节一致。`render::Font` 保留私有、有界 LRU 缓存：每个字体复用一个 `hb_font`/`hb_buffer`
（单一 `hb_face`，因此 harfbuzz 的 shape-plan 缓存生效）、按精确 26.6 字号 + UTF-8 文本索引的
shaped-run 缓存（16 MiB / 16k entries）、按 glyph id + 26.6 字号索引的 glyph 覆盖率缓存
（32 MiB / 16k entries）；分配失败视为 miss，逐出不改变输出。shell 的 `Fonts` 增加无分配的解析
样式快速路径与稳定 style id、文本度量 LRU（32 MiB / 100k）与 run LRU（16 MiB / 100k）；位图
退化路径绕过这些缓存。`paint` 把显示列表裁剪到视口：填充/图片按精确目标矩形相交、文本盒按四周
扩大 `ceil(2 * font_size_px)` 后的矩形判定（覆盖 glyph overhang/斜体/抗锯齿），裁剪发生在
样式/图片/字体 run 查找之前，同时保留一个 viewport clip 对与命令顺序；渲染像素与不裁剪逐字节
相同。窗口循环在 Expose 时只重绘不重排，只有窗口尺寸真正变化才重排；X11 后端合并连续的
Expose/ConfigureNotify 队列事件（输入/close 事件永不合并、丢弃或乱序）；每个窗口尺寸复用一个
`render::Surface`。因此一帧成本是 O(可见命令 + 一次盒树遍历)，而不是 O(整页 shaping/光栅化)；
重排仍是全量 reflow，只是被度量缓存变便宜。渲染仍是单线程、纯软件：实测填充率不是瓶颈
（系统字体在 800×600 与 1600×1000 下光栅化耗时相同），因此没有 GPU 后端；窗口仍用
`XPutImage` 呈现整个 surface（XShm/dirty-rect 留待后续，需要新依赖审批）。在 8.1 万文本盒压力页
（800×600、自动系统字体）上实测：单帧绘制 10.6 s -> 3.5 ms，布局 2.1 s -> 31 ms，页面加载
2.2 s -> 69 ms，且所有 M11 之前的 fixture 截图与 33 个滚动参考帧逐字节相同。

导航（M7）：`shell::Browser` 拥有一个浏览器会话：当前页面（最终 URL、DOM、样式树、盒树、解码后的图片与字体）、
导航历史、整数垂直滚动偏移与绘制。历史是一个 URL 向量加一个索引；每次成功加载追加一条
（后退后再加载会截断前进条目），加载失败则页面、滚动偏移与历史都不变。点击先用滚动偏移换算成
页面坐标，再对盒树做命中测试（`layout::HitTest` 返回绘制顺序中最后一个包含该点的盒），沿 DOM
祖先找到最近的带 `href` 属性的元素，用 `net::ResolveUrl` 相对页面最终 URL 解析后加载。只有 `http`/`https`/`file` 引用会导航；只改变 fragment 的引用是 no-op。滚动是垂直的、以整数设备像素表示，
由 `layout::ContentHeight` 钳制在 `[0, content_height - viewport_height]`；`paint` 用视口裁剪
包裹页面并按 `-scroll_y` 平移，`render` 执行冻结的裁剪栈。窗口事件循环把点击、滚轮格数与按键
（Alt+Left/Right 前进后退，方向键/PageUp/PageDown/Home/End 滚动）转发给会话并按需重绘；窗口
标题跟随第一个 `<title>` 文本，没有时回退到 URL。加载在处理函数内同步执行，保持单线程 MVP 模型。页面加载器（M13 起每次加载一个 keep-alive `net::Client` 加页内图片 URL 去重）、外部样式表发现/抓取与图片映射是 shell 的私有实现细节：`toy/shell/browser.h` 保持相同的公开方法，并用前向声明与 out-of-line 析构函数隐藏页面数据。

## 数据模型

- **DOM**：一个用整数 `NodeId` 寻址的 arena（`std::vector<Node>`）；父节点、首/末子节点、前/后兄弟链接都是 id。属性存在每个节点的小 vector 里。没有父/子指针环，没有 `shared_ptr`。
- **样式树**：按 DOM id 索引的计算值组成的并行结构。
- **盒树**：用整数 `BoxId` 寻址的布局 arena（与 DOM 同构）；每个盒引用其 DOM 来源（元素或文本节点 id），并持有一个以 CSS 像素表示的绝对 border-box 矩形。文本盒保存放置在该行上的那个单词。匿名盒没有 DOM 来源。
- **显示列表**：命令的扁平 `std::vector`（`FillRect`、`DrawText`、`DrawImage`、`PushClip`/`PopClip`）；图片命令持有对不可变 `render::Image` 的共享引用，real-font 文本命令持有共享 `render::Font` 并用基线 `y`（位图文本保持冻结的 cell 顶部 `y`、font 为空）。命令使用整数设备像素。
- **单位**：样式/布局中 CSS 像素用 `float`；光栅化器中设备像素用 `int`。MVP 假设 1 CSS px = 1 设备 px（无缩放，无 DPI 缩放）。

选择 arena + id，是因为这些树平凡可析构、在测试中容易 dump，而且不可能形成所有权环。只有当它确实带来痛苦时才重新考虑。

## 线程与事件循环

- 一个进程、一个线程、一个事件循环。MVP 中没有工作线程。`render::Font` 在度量/绘制之间缓存可变的 freetype face 字号，因此刻意只支持单线程；不要跨线程共享同一个 `Font`。`net::Client` 同样单线程并拥有每次页面加载的连接池；不要跨线程共享 Client，也不要在其上流水线请求。
- 页面加载在导航回调中同步执行；加载期间窗口无响应。对玩具项目来说可以接受，只有当真成为问题时才引入异步网络。
- `net::Fetch` 是接口后面的阻塞调用，因此以后可以替换成非阻塞状态机而不触碰调用方。
- 平台事件循环等待窗口事件，并把 close/resize/input/paint 请求转发给 `shell`；由 `shell` 决定重绘什么。重绘按需发生（expose、resize、scroll），不靠定时器。自 M11 起 Expose 只重绘（页面已布局）、resize 只有窗口尺寸真正变化才重排，X11 后端会先合并队列中的 Expose/ConfigureNotify burst 再交付一次重绘/重排请求；输入与 close 事件永不合并或乱序。
- 没有全局可变状态：应用对象拥有所有模块。

## 渲染模型

- 只做软件光栅化；没有 GPU，没有渲染线程。M11 的实测表明填充率不是瓶颈（成本在 shaping/字形处理），因此刻意不引入 GPU 后端；软件缓存与视口裁剪已足够。
- Surface：32 位 ARGB、straight alpha（非预乘 alpha）、SrcOver 混合、左上角原点。
- 裁剪是由显示列表命令压入的整数矩形栈；初始裁剪是整个 surface，光栅化器的工作量受裁剪区域限制（因此对抗性输入如超大字号也不会让绘制无界）。
- `shell` 在绘制前把 canvas 填白；元素的 `background-color` 绘制为该元素的盒（M6 不会把 `html`/`body` 背景传播到 canvas）。
- 自 M10 起默认文本使用自动选择的真实字体：由 harfbuzz 整形、freetype 光栅化为 8-bit 覆盖率，放在布局提供的行基线上，以覆盖率为 alpha 与不透明目标做 SrcOver 混合；字形裁剪到当前 clip，有效字号上限 4096 px。没有可用系统字体时回退到小型内置位图字体（手写 8x16 ASCII 点阵，最近邻缩放、无抗锯齿、非 ASCII 渲染 `?`），该路径的像素冻结并作为测试基线。
- 图片被解码为直通 alpha 的 ARGB（界限：每边最多 8192 px，总计最多 16,777,216 像素），并以最近邻缩放绘制：目标像素 (dx, dy) 用整数向下取整采样源 `((dx - x) * width / dst_width, (dy - y) * height / dst_height)`，因此缩放不引入插值或抗锯齿。图片像素与矩形填充使用相同的 SrcOver 公式混合。
- real-font 文本是唯一使用抗锯齿的绘制路径：freetype 覆盖率用同一 SrcOver 公式混合，逐字形整数放置把 26.6 pen 位置四舍五入到设备像素（远离零），因此在给定字体、字号与 freetype/harfbuzz 版本下输出确定。测试断言着墨区域与 AA 存在性，而不是精确覆盖率字节（后者依赖库版本）。
- 绘制顺序是盒树前序：一个盒先画自己的背景，再画子节点；还没有层叠上下文或 z-index，因此重叠的盒按文档顺序绘制。
- 呈现：平台接收一个像素视图（像素、宽度、高度、stride 以每行像素数为单位——与 `render::Surface::stride()` 相同的单位）并把它复制出去。headless 后端改为写一个 PPM 文件，这让渲染测试不依赖任何东西。
- **headless 优先**：每个阶段都不开窗口运行。页面加载、解析、布局、绘制和截图都通过 CLI 模式（`--dump-body`、`--dump-dom`、`--dump-style`、`--dump-boxes`、`--screenshot [url]`）驱动和检查；窗口只呈现 headless 路径已经产生的像素。自动化测试从不需要显示器。
- M1 的占位页（白色页面加蓝色横条）在没有 URL 时仍是回退路径，因此没有页面也能验证像素通路；真实页面走 M6 的绘制路径。

## 错误与所有权

- 今天的可恢复失败使用简单、显式的结果：`std::optional<T>`（URL 解析）或 `bool` 加一个 `std::string* error` 人类可读消息（抓取、窗口创建）。调用方直接检查结果，从不依赖异常。只有当简单模式开始带来痛苦时，才引入带模块错误枚举的 `Result<T>` 类型。
- 不跨模块边界抛异常，普通控制流不用异常；内部不变量在 debug 构建中用 `assert` 失败。
- 所有权：`std::unique_ptr` 和值拥有；引用/裸指针不拥有；arena id 留在树内部。除非确实存在两个所有者，否则不用 `shared_ptr`。

## 仓库布局

```
.
├── .github/workflows/ci.yml        # 与本地门禁一致的 GCC + Clang 构建/测试 CI
├── AGENTS.md
├── AI-DISCLOSURE.md                # 显眼的“大量使用 AI”声明
├── CMakeLists.txt
├── CMakePresets.json               # 便捷 presets：default（普通构建）、dev（测试 + WERROR）
├── CONTRIBUTING.md
├── LICENSE
├── README.md / README.zh.md
├── SECURITY.md
├── docs/
│   ├── assets/                     # README.md/README.zh.md 使用的截图
│   └── <设计与流程文档>
├── src/
│   ├── main.cpp
│   └── <module>/                    # 与模块首个真实文件一起创建
│       ├── include/toy/<module>/*.h # 公开 API: #include "toy/<module>/<file>.h"
│       └── *.cpp                    # 实现与私有头文件
└── tests/
    ├── CMakeLists.txt
    ├── toolchain_smoke.cpp
    ├── fixtures/                    # 提交的 fixture；二进制 fixture 随附生成器
    │   └── <kind>/{README.md,generate*.py,<fixture files>}
    └── <module>/                    # 每个测试主题一个可执行文件
```

- 没有占位目录：模块目录与其第一个文件一起创建。
- 每个模块变成一个 CMake 静态库目标 `toy_<module>`；`toy_shell` 被链接进 `toybrowser` 可执行文件。每个目标都获得 `toybrowser_set_warnings()`。
- 只有 `include/` 是公开的；其他一切都是模块私有的。

## 测试挂点（随需要它们的阶段一起加入）

- CLI 标志：`--dump-body`、`--dump-dom`、`--dump-style`、`--dump-boxes`、`--screenshot <file> [url]`、`--headless`，以及窗口模式可选的位置参数页面 URL。
- 测试是注册到 CTest 的普通 C++ 可执行文件；新测试共享 header-only 的 `tests/support/check.h`（接口目标 `toy_test_support`）。没有测试框架依赖。
- 解析器/布局的 golden 文件是逐行比较的文本 dump。截图是确定性的（整数像素、无抗锯齿），因此绘制测试精确比较像素，跨编译器运行产出逐字节相同的 PPM；不需要图像容差。
- 测试是确定性的，从不需要网络、显示器、已安装字体或特定 locale。
- M8b real-font 测试使用提交的自造 `tests/fixtures/fonts/toy_test.ttf`（从不使用已安装字体）：`render.font_ttf` 覆盖加载/度量/整形/钳制，扩展后的 `render.paint` 覆盖 AA 覆盖率/裁剪/基线/no-op，`shell.fonts` 覆盖端到端 real-font 页面，CLI 检查覆盖 `--font`、`--font-family`（配合测试局部 `FONTCONFIG_FILE`）、错误退出码与截图输出。
- M9 TLS 测试使用 `tests/fixtures/tls/` 中提交的仅测试用证书（CA、`localhost` 叶证书与无关的不受信任叶证书；README 记录重新生成方法）与 header-only 的进程内 TLS loopback 服务器 `tests/support/tls_server.h`。信任只通过 CTest `ENVIRONMENT` 属性（`SSL_CERT_FILE=<测试 CA>`）提供，因此走的是正常校验路径。`net.tls` 覆盖成功 GET、不受信任证书、主机名不匹配、握手超时、http<->https 重定向与 body 上限；`shell.https` 通过 `shell::Browser` 用 TLS 提供提交的 HTML+PNG 页面（`tests/fixtures/https_site/`）并断言渲染像素，以及不受信任证书失败路径。没有测试使用公网或公共 CA，M7 导航 fixture 的不支持 scheme 用例改为离线 `ftp://` 链接。
- M10 字体测试使用提交的自造 fixtures（`tests/fixtures/fonts/`：常规、粗体、斜体与一个小汉字
  face）与两个生成的 fontconfig 配置：fixture 配置（只有 fixture 字体、通用族映射到拉丁 face）用于
  自动选择、weight/style、未知族回退与混合拉丁+汉字 run 切分；空配置（无字体目录）强制位图路径，
  供所有 M10 之前的 golden 使用。`style.parser`、`layout.baseline`、`layout.cjk_line_break`、
  `shell.font_selection`、`shell.page_language`、`shell.cjk_*`（逐单元文本盒、换行、绘制像素）与
  更新后的 `shell.fonts*` 覆盖该特性；测试从不使用已安装字体或机器字体配置。
- M8a 图片测试使用像素内容有文档的手工生成 PNG fixtures（`tests/fixtures/images/`）与提交的图片演示站点（`tests/fixtures/img_site/`）：解码器被直接测试，布局用假 replaced 尺寸测试，shell 端到端通过 `shell::Browser` 以及 CLI 的 `--dump-boxes`/`--screenshot` 检查。
- M17 JPEG 测试离线且确定：`tests/fixtures/images/` 提交自造 JPEG fixtures（baseline RGB 四象限、
  灰度、progressive、Adobe CMYK、9000x1 超宽与截断流）以及基于已批准 libjpeg 的提交生成器
  `generate_jpeg.cpp`；fixture README 记录像素内容、每通道容差、CMYK/警告策略与重新生成命令
  （JPEG 有损，测试绝不比较整文件或要求逐位精确电平）。`render.jpeg` 覆盖解码、边界、失败时
  `out` 不变、`DecodeImage` 分派（PNG 精确像素/JPEG/垃圾数据）、两个入口的截断输入、非法数据与
  确定性。`tests/fixtures/jpeg_site/` 加 `shell.jpeg`/`shell.jpeg_dump_boxes` 通过
  `LoadPage`/`Browser` 覆盖混合 PNG/JPEG 页面：固有尺寸、CSS 缩放、URL 共享缓存、缺失/截断静默
  跳过、冻结盒几何与容差内像素（PNG 像素精确）。M17 之前的字节兼容门禁把上一提交的构建与新版
  比较（25 用例：PNG/CSS 页面、dump、截图、退出码必须逐字节相同）；真实站点 JPEG 证据留在
  `.tmp/`，绝不提交。
- M12 外部样式表测试使用提交的 `tests/fixtures/css_site/` 站点（子目录相对样式表、文档序排序、
  被过滤/失败/重复链接、无样式表与全被过滤页面，以及有文档的期望计算值、几何与像素），并加上
  header-only 进程内 loopback HTTP 服务器 `tests/support/http_server.h` 覆盖重定向、绝对 URL、
  相对最终 URL 解析、失败与不去重请求数等用例。`style.external_sheets` 直接覆盖级联 API，`shell.external_css` 覆盖 shell 端到端（含
  `Browser`/绘制像素），CLI 检查覆盖带外部样式表的 `--dump-style`。没有测试使用公网。
- M13 资源加载测试全部离线并断言计数、绝不断言耗时。`tests/support/http_server.h` 与
  `tests/support/tls_server.h` 现在能让一条连接服务多个请求并暴露连接/请求计数（HTTP server 还能
  强制关闭响应以模拟过期空闲连接）。`net.keep_alive` 覆盖同 origin 复用（Content-Length/chunked）、
  `Connection: close`、两个 origin、过期连接重试、新连接失败、关闭空闲连接、`file://`、重定向、
  HTTP/1.0 规则与缺少长度定界错误；`net.gzip` 覆盖 gzip 解码、identity、损坏/不支持编码、解码后大小
  上限，以及一次性 `Fetch()` 仍不发送 `Accept-Encoding`；`net.tls_keep_alive` 证明两次 https 请求
  复用一条 TLS 连接；`shell.resource_loading` 从 loopback server 提供页面 + 样式表 + 重复图片，断言
  一条连接、每个唯一图片 URL 一次抓取、共享解码图片、重复坏图片只抓一次，以及被强制关闭的空闲连接后
  的恢复。

- M14a 的 CSS 值/排版测试离线且确定：提交的 `tests/fixtures/css_compat_site/`（`values.html`、
  `typography.html`、`boxes.html`、`legacy.html` 加 README，记录每个期望计算值、盒矩形与像素），
  `style.values` 覆盖单位/颜色/background 简写/逻辑映射/auto margin/约束，`style.typography` 覆盖
  line-height/text-align/visibility/text-decoration 的继承与 `!important`，`layout.style_properties`
  用 fake measurer 精确断言百分比解析、钳制、auto margin 居中、line-height 基线与 text-align 平移，
  `shell.css_compat` 端到端（LoadPage 计算值 + 冻结盒几何 + Browser 像素），
  `shell.dump_style_css_compat` 检查追加的 dump 字段，`shell.css_compat_screenshot` 检查截图 CLI；
  扩展后的 `paint.display_list` 覆盖 visibility 与下划线命令的几何/顺序/裁剪以及手写 M14 之前参考
  列表。`legacy.html` 证明 M14 之前页面保持逐字节盒 dump/PPM；字节兼容门禁只规范化追加的样式字段。
  手工真实站点检查不进入 CTest，`.tmp/` 证据从不提交。
- M14b 盒装饰测试离线且确定：提交的 `tests/fixtures/css_compat_site/` 新增 `borders.html`
  （1–4 边、不同宽度/样式/颜色/currentColor/rgba、简写、content-box vs border-box、嵌套 border
  盒）、`overflow.html`（超大被裁剪子元素 + 文本、visible 对照、带 border 的裁剪、后续盒）与
  `decorations.html`（三种装饰各自/组合/继承/重置），README 记录每个期望计算值、盒矩形、装饰行与
  像素。`style.borders` 覆盖宽度/样式/颜色长写与简写重置/拒绝语义，`style.box_model` 覆盖
  box-sizing/overflow，`layout.box_model` 覆盖 border 几何/box-sizing/auto margin/高度/行内
  border/none-hidden/replaced 忽略与 M14b 之前几何相等；扩展后的 `paint.display_list` 覆盖 border
  矩形顺序/几何/颜色/alpha/裁剪、overflow clip 对与精确裁剪像素、line-through/overline 行（位图
  与真实字体）以及手写 M14b 之前参考列表。`shell.css_compat` 断言新页面的计算值、冻结盒几何与
  Browser 像素；`shell.dump_style_css_compat_borders` 与 `shell.css_compat_screenshot_borders`
  覆盖 CLI。字节兼容门禁对 M14a 二进制比较（盒 dump/PPM/退出码相同；样式 dump 从 ` min-width=`
  规范化），与更早的 M14（M13）二进制比较只在 M14a fixture 页面上有差异。真实站点证据留在 `.tmp/`，
  从不进入仓库/CTest。

## 参考资料

行为首先对照标准、其次对照真实浏览器来检查；两者都只是参考——绝不复制它们的代码：

- HTML Living Standard：<https://html.spec.whatwg.org/multipage/>
- DOM Standard：<https://dom.spec.whatwg.org/>
- URL Standard：<https://url.spec.whatwg.org/>
- CSS Syntax Level 3：<https://www.w3.org/TR/css-syntax-3/>
- Selectors Level 3：<https://www.w3.org/TR/selectors-3/>
- CSS Cascading and Inheritance Level 4：<https://www.w3.org/TR/css-cascade-4/>
- CSS Values and Units Level 3：<https://www.w3.org/TR/css-values-3/>
- HTTP Semantics（RFC 9110）与 HTTP/1.1（RFC 9112）；TLS 1.2/1.3（RFC 5246/RFC 8446）
- OpenSSL 文档（M9 已批准依赖，自 M9 起链接）：<https://docs.openssl.org/>
- PNG（第三版）：<https://www.w3.org/TR/png-3/> 与 libpng 手册（M8a 已批准依赖）：<https://www.libpng.org/pub/png/libpng.html>
- FreeType、HarfBuzz 与 fontconfig 文档（M8b 已链接依赖）：<https://freetype.org/>、<https://harfbuzz.github.io/>、<https://fontconfig.pages.freedesktop.org/fontconfig/>
- Web Platform Tests：<https://github.com/web-platform-tests/wpt>

## 刻意排除的范围

GPU 渲染、多进程架构、沙箱、插件系统、遥测、配置框架、自定义日志框架。功能只在里程碑需要时落地。
