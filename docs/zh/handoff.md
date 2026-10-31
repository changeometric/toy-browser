> 本文为中文翻译，英文原版为准：[handoff.md](../handoff.md)。

# 交接 —— 当前状态与接管指南

本文件是接手仓库的团队所用的快照。规范的阅读顺序在 [docs/README.md](README.md)；硬性规则在 [AGENTS.md](../../AGENTS.md)。

## 项目当前状态

已完成的阶段（细节与验收见 [roadmap.md](roadmap.md)）：

| 阶段 | 交付内容 |
| --- | --- |
| M0 | 设计文档、CMake 骨架、桩 CLI、工具链冒烟测试 |
| M1 | X11 窗口、软件 `Surface`、headless PPM 截图（`--headless --screenshot`） |
| M2 | URL 解析、HTTP/1.1 GET、`file://` 抓取（`--dump-body`） |
| M3 | HTML tokenizer/树构建器、arena DOM、确定性 dump（`--dump-dom`） |
| M4 | CSS 解析器/选择器、UA + 作者层叠、计算样式（`--dump-style`） |
| M5 | `layout` 盒树：块级/行内/匿名盒与换行，`render` 中的位图字体度量，确定性盒 dump（`--dump-boxes`） |
| M6 | `render` 显示列表 + 软件光栅化器 + 手写 8x16 ASCII 字形，`paint` 盒树转显示列表模块，窗口/`--screenshot` 绘制与可选的位置参数页面 URL |
| M7 | 导航：headless `shell::Browser`（最终 URL、历史、滚动、标题）、`net::ResolveUrl`、盒命中测试、滚动裁剪绘制、X11 点击/滚轮/按键 + 窗口标题、提交的演示站点、`shell.navigation` 测试 |
| M8a | PNG 图片：`render::Image` + 封装在 `render::DecodePng` 后的 libpng 解码器、`DrawImage` 显示列表命令/光栅化器、按调用方提供的固有尺寸布局 replaced 行内 `<img>`、私有 shell 页面加载器（抓取/解析/解码/跳过失败）、图片 fixtures 与 `tests/fixtures/img_site/`、46 个测试 |
| M8b | 真实字体：冻结 pimpl 后的 `render::Font`（freetype 度量/光栅化 + harfbuzz 整形、4096 px 有效字号上限）、按基线放置的抗锯齿 `DrawTextWithFont`、`--font PATH` / `--font-family NAME`（fontconfig）选择、自造字体 fixture 与 `tests/fixtures/font_site/`、55 个测试；位图路径保持逐字节不变 |
| M9 | `https://` 抓取：`net` URL 层接受 https（默认端口 443）；保持阻塞 `Fetch()` 不变，内部接入 OpenSSL TLS 传输，并通过私有字节流抽象与 HTTP/1.1 代码共用；证书与主机名校验始终开启并使用 OpenSSL 默认信任库（无 insecure 模式）、SNI、沿用现有超时；重定向改用 `net::ResolveUrl`（相对/协议相对、http<->https）；提交测试 CA/叶证书、进程内 TLS loopback 服务器、`net.tls` + `shell.https` 端到端测试、57 个测试；http/file 输出逐字节不变 |
| 审阅轮 | 修复无上限的 `--size`、文件 body 上限、dump 缩进；20 个 CTest 测试 |

**下一计划里程碑：M10 —— HTTP 代理（已计划，尚未开始）。** M9（最基础 HTTPS）已交付；用户于
2026-10-31 批准 OpenSSL 用于 TLS，并自 M9 起链接（记录在 [dependencies.md](dependencies.md)）。
M10 通过 `FetchOptions` 支持 HTTP 代理（`http` 用 absolute-form，`https` 用 `CONNECT`），这是在
受限网络上让真实站点测试变得实用的能力。JavaScript 子集已被用户暂缓。

尚未实现：代理（计划 M10）、JavaScript（已暂缓）、外部样式表、border、水平滚动/滚动条、
hover/active 链接样式与表单控件，以及 CSS 驱动的字体选择
（`font-family`/`font-weight`/`font-style`、fallback 链、`@font-face`）。PNG 图片自 M8a 起可用、
真实 TTF 文本自 M8b 起可用（opt-in）、`https://` 自 M9 起可用（已知限制见 [roadmap.md](roadmap.md)）；
`http://` 与 `file://` 仍受支持。窗口与 `--screenshot` 在给出 URL 时绘制真实页面，否则显示 M1 占位页；
给出页面 URL 时窗口也可浏览（链接、滚轮/键盘滚动、Alt+Left/Right 历史、`<title>`）。`--dump-*`
模式仍是主要调试路径。

## 验证当前状态（无需上下文）

以下命令请在仓库根目录执行：

```sh
BUILD=build/onboard-check
# 测试是 opt-in 的（默认构建只编译浏览器本体）；验证时传入该标志。
cmake -S . -B "$BUILD" -DTOYBROWSER_BUILD_TESTS=ON -DTOYBROWSER_WERROR=ON
cmake --build "$BUILD"
ctest --test-dir "$BUILD" --output-on-failure

"$BUILD"/toybrowser --help
"$BUILD"/toybrowser --dump-dom    "file://$PWD/README.md" | head
"$BUILD"/toybrowser --dump-style  "file://$PWD/README.md" | head
"$BUILD"/toybrowser --dump-boxes  "file://$PWD/README.md" | head
"$BUILD"/toybrowser --headless --screenshot /tmp/toybrowser.ppm && file /tmp/toybrowser.ppm
"$BUILD"/toybrowser --size 640x400 --screenshot /tmp/toybrowser-page.ppm \
    "file://$PWD/README.md" && file /tmp/toybrowser-page.ppm
```

预期：57/57 测试通过；使用 `-DTOYBROWSER_WERROR=ON` 构建在 GCC 和 Clang 上无警告；dump 输出确定性的树；截图写出二进制 PPM（带 URL 的形式绘制真实页面，不带 URL 的形式保留占位页）。所有 headless 模式都不需要显示器。需要 CMake 3.20 或更新版本；构建需要 X11、libpng、freetype、harfbuzz、fontconfig 与 OpenSSL 开发头文件（均为已批准的依赖）；只有窗口模式需要运行中的显示服务器。

```sh
"$BUILD"/toybrowser --size 320x200 --screenshot /tmp/toybrowser-img.ppm \
    "file://$PWD/tests/fixtures/img_site/index.html" && file /tmp/toybrowser-img.ppm
"$BUILD"/toybrowser --size 320x200 --dump-boxes \
    "file://$PWD/tests/fixtures/img_site/index.html" | grep '<img>'

# 真实字体：提交的自造 fixture 是确定性的；已安装字体只用于手工演示（如
# --font-family "sans-serif"）。
"$BUILD"/toybrowser --font tests/fixtures/fonts/toy_test.ttf --size 320x600 \
    --screenshot /tmp/toybrowser-font.ppm "file://$PWD/tests/fixtures/font_site/index.html" \
    && file /tmp/toybrowser-font.ppm
"$BUILD"/toybrowser --font tests/fixtures/fonts/toy_test.ttf --size 320x200 --dump-boxes \
    "file://$PWD/tests/fixtures/font_site/index.html"
"$BUILD"/toybrowser --font-family "sans-serif" --size 400x200 \
    --screenshot /tmp/toybrowser-system-font.ppm "file://$PWD/tests/fixtures/nav_site/index.html"
```

预期：图片截图绘制提交的 PNG fixtures（旗子、alpha、灰度、调色板），盒 dump 包含带固有/CSS 尺寸的
`<img> inline` 盒；`missing.png` 不保留盒。fixture 字体的盒 dump 显示真实字体度量（例如 32 px 下
`<h1>` 文本 "ABC" 宽 58，而不是位图字体的 48）；不给字体标志的页面保持位图输出。

```sh
"$BUILD"/toybrowser --size 400x200 --screenshot /tmp/toybrowser-nav.ppm \
    "file://$PWD/tests/fixtures/nav_site/index.html" && file /tmp/toybrowser-nav.ppm

# HTTPS（M9）：确定性 TLS 测试使用提交的测试证书（tests/fixtures/tls/）与进程内 loopback
# 服务器，从不访问公网。
ctest --test-dir "$BUILD" -R '^(net\.tls|shell\.https)$' --output-on-failure

# 手工真实站点检查：依赖网络，不属于测试套件。
"$BUILD"/toybrowser --dump-body https://example.com/ | head
"$BUILD"/toybrowser --size 800x600 --screenshot /tmp/toybrowser-https.ppm \
    https://example.com/ && file /tmp/toybrowser-https.ppm
```

预期：`net.tls` 与 `shell.https` 通过；公网 `https://` 页面可抓取并绘制。证书、主机名与超时失败是
普通 fetch 错误（页面模式 CLI 退出码 1），校验始终开启，没有 insecure 模式。

窗口演示（需要显示器）：

```sh
"$BUILD"/toybrowser --size 320x600 "file://$PWD/tests/fixtures/img_site/index.html"
"$BUILD"/toybrowser --font-family "sans-serif" --size 400x200 \
    "file://$PWD/tests/fixtures/nav_site/index.html"
# 图片页：确认 PNG 图片被绘制；真实字体页：确认抗锯齿文本；导航页：点击 "About page"，用滚轮/
# 方向键/PageUp/PageDown/Home/End 滚动，Alt+Left/Right 前进后退；窗口标题跟随 <title>。
```

## 下一轮：M10 —— HTTP 代理（已计划；等用户确认后开始）

**状态：** 只有设计草案（见 [roadmap.md](roadmap.md)）；还没有 M10 代码。M9 已完成且全绿，因此
起点就是当前 HEAD。

- 目标：`http://` 通过 HTTP 代理以 absolute-form 请求目标抓取；`https://` 通过代理 `CONNECT` 隧道后
  再做正常的 TLS 握手。浏览器自身的代理支持是在受限网络上让真实站点测试变得实用的能力。
- 预期形态（契约在该轮开始时冻结）：`--proxy host:port` 加标准 `http_proxy`/`https_proxy` 环境变量
  回退，全部留在 `net` 内、位于 `FetchOptions` 之后；预计不需要新的第三方依赖；进程内假代理测试
  （不访问公网），未配置代理时输出逐字节不变。
- 在重新划范围之前不做：SOCKS 代理、代理认证、bypass 列表、PAC。

M10 之后总路线图就全部变绿；剩余工作是已记录的 backlog（更多图片格式、CSS 字体选择、外部样式表等）
以及用户的新优先级。

## 仓库地图

- `src/<module>/` —— 每个模块一个 CMake 库目标（`base` 尚不存在；模块图与依赖方向见 [architecture.md](architecture.md)）。
- `src/main.cpp` —— 只做 CLI 解析；`shell` 编排 fetch → parse → style → images → layout → paint →
  dump/present，`toy/shell/browser.h` 拥有一个浏览器会话（历史、滚动、点击、标题）。
- `src/net/` —— URL（`url.cpp`）、共享的 HTTP/1.1 请求/响应代码（`http.cpp`）、以及 M9 的私有传输层
  拆分：`socket_internal.h`/`socket.cpp`（TCP 连接 + poll 辅助）、`stream_internal.h`/`stream.cpp`
  （私有 `Stream` 接口、纯 `SocketStream` 与 `ReadUntilEof`）、`tls_internal.h`/`tls.cpp`
  （OpenSSL 实现的 `TlsStream`、进程级 `SSL_CTX`、校验/握手循环、SIGPIPE 保护）。OpenSSL 类型不出现在
  公开的 `include/` 头文件中；`net` 唯一的公开 API 是 `toy/net/url.h` + `toy/net/http.h`。
- `src/shell/page.h` —— shell 私有的页面加载器（最终 URL、DOM、样式、盒树、解码后的图片）；
  `src/render/include/toy/render/image.h` —— `Image` 与基于 libpng 的 `DecodePng`；
  `src/render/include/toy/render/font.h` —— 位图字体度量加真实 `Font` API（加载、度量、整形 advance；
  公开头文件不含 freetype/harfbuzz 类型），实现位于 `src/render/font_ttf.cpp`；显示列表与光栅化器在
  `toy/render/paint.h`，`src/paint/` 从盒树构建列表且不光栅化。`src/shell/application.cpp` 负责字体
  选择（`--font`/`--font-family`，唯一使用 fontconfig 的地方）。`layout` 通过 `shell` 提供的回调
  消费文本度量，自身不依赖 `render`。
- `tests/<module>/` —— 每个主题一个可执行文件，注册到 CTest；新测试使用 header-only 的
  `tests/support/check.h`。提交的 fixtures：`tests/fixtures/nav_site/`（三页导航演示；它的
  “Unsupported page” 链接是故意的不支持 scheme 用例，任何测试都不会访问公网）、
  `tests/fixtures/img_site/`（图片流水线演示）、`tests/fixtures/font_site/`（真实字体 + 图片演示）、
  `tests/fixtures/images/`（像素内容有文档的手工 PNG）、`tests/fixtures/fonts/`（自造最小 TTF，
  度量/字体族有文档；测试从不使用已安装字体）、`tests/fixtures/https_site/`（shell 测试通过 TLS
  提供的 HTML+PNG 页面）与 `tests/fixtures/tls/`（提交的仅测试用 CA、`localhost` 叶证书与不受信任叶
  证书/私钥；重新生成方法见其 README）。`tests/support/tls_server.h` 是 `net.tls`/`net.http`/
  `shell.https` 共用的 header-only 进程内 TLS loopback 服务器；没有任何测试访问公网。
- `docs/` —— 架构、路线图、约定、团队流程、决策、依赖、待决问题（本文件是快照）。
- 共享任务板 —— 由 agent 运行时提供（任务 create/claim/update，带 revision）；如果运行时没有，则用 `docs/tasks/<milestone>.md`，字段相同。见 [team-workflow.md](team-workflow.md)。
- `README.md` / `README.zh.md` 与 `docs/zh/` —— 面向用户的入口（英语为权威）。
- `.tmp/` —— 被 gitignore 的本地暂存区，用于笔记、fixture、下载与测试输出。

## 冻结契约（不要随意改动）

- CLI 模式、标志与退出码：`0` 成功，`1` 运行时错误，`2` 用法错误。可选的位置参数页面 URL（`toybrowser [options] [url]`）只用于窗口模式和 `--screenshot`；最多一个 URL，不能与 `--dump-*` 模式组合，不给 URL 时保留 M1 占位页。M8b 新增 `--font PATH` 与 `--font-family NAME`：各自需要非空参数，二者互斥，且与 `--dump-body`/`--dump-dom`/`--dump-style` 组合是用法错误（退出码 2）；允许与窗口模式、`--screenshot`、`--dump-boxes` 组合。选中的字体无法加载/解析是运行时错误（退出码 1）；两个标志都不给时使用内置位图字体（M8b 之前的行为）。
- Dump 格式（DOM、style、boxes）：确定性、面向行、由测试断言；任何改动都需要同一轮内更新测试和文档。盒 dump 是前序遍历的行：`<tag> block|inline`、`anonymous block` 或 `text "..."`，后接 `x= y= width= height=`，全部是绝对 CSS 像素取整后的整数（`std::lround`）；缩进为每层盒深度两个空格，上限 64 层。
- 布局语义通过布局测试与 M5/M8a 的 known-limits 冻结：视口宽度来自 `--size`，没有 margin 合并，行顶部对齐，文本盒以单词为单位，仅按 ASCII 空白换行。M8a replaced 元素：`LayoutOptions::replaced_sizes`（DOM 节点 id → 固有 CSS px）把行内元素变成原子行内级盒；CSS `width`/`height` px 覆盖固有尺寸，只指定一侧时保持固有宽高比，盒整体换行、抬高行高并顶部对齐；其 dump 行仍是 `<tag> inline x= y= width= height=`，没有子节点，`display:block` 的 replaced 元素不支持，replaced 盒的 margin/padding 被忽略。没有条目的页面保持原有几何逐字节不变。
- 绘制语义通过 M6/M7 测试冻结：显示列表命令使用整数设备像素（`FillRect`、`DrawText`、`DrawImage`、`PushClip`/`PopClip`）；填充对不透明目标使用 SrcOver 公式 `(src * a + dst * (255 - a) + 127) / 255`；初始裁剪是整个 surface，光栅化工作量受裁剪限制；`shell` 把 canvas 填白；绘制顺序是盒树前序（先背景、再图片、再子节点/文本）；文本使用内置 8x16 ASCII 字形，最近邻缩放、无抗锯齿、非 ASCII 用 `?`、文本像素不透明。
  M7 绘制：非空页面恰好被一个 `PushClip(0, 0, viewport)`/`PopClip` 对包裹，每条命令的 y 按
  `-scroll_y`（整数设备像素，x 不变）平移；没有图片的页面在滚动 0 必须与 M6/M7 逐字节一致。
  M8a 图片：节点 id 在 `paint::ImageMap` 中的行内 replaced 盒各发出一条 `DrawImage`，在该盒背景之后；
  命令持有 `shared_ptr<const render::Image>`；目标像素 (dx, dy) 用整数向下取整采样源
  `((dx - x) * image.width / width, (dy - y) * image.height / height)`（最近邻，无插值/抗锯齿），
  用同一 SrcOver 公式混合（alpha 0 无操作，alpha 255 写 `0xFF000000 | rgb`，结果不透明）；空图片映射
  精确复现 M8a 之前的命令列表。M8b real-font 文本：会话选择字体时，每个文本盒变成
  `DrawTextWithFont` 命令（x = pen 原点，y = 设备像素基线，基线 =
  `round(box.y) - scroll_y + round(font->AscentPx(size))`），字体按共享引用保留，字形由 harfbuzz
  整形、freetype 光栅化为 8-bit 覆盖率，按冻结的逐字形整数公式放置（26.6 pen 四舍五入到设备像素、
  远离零，再加 freetype bitmap_left/top），用同一 SrcOver 公式以覆盖率为 alpha 混合（结果不透明），
  裁剪到当前 clip，有效字号钳制到 `kMaxRealFontSizePx`（4096 px）。没有选择字体时精确输出冻结的
  位图命令列表（`DrawText`、cell 顶部 y、不透明无抗锯齿像素）。
- Real-font 契约（M8b）：`render::Font` 加载后不可变（pimpl，公开头文件不出现 freetype/harfbuzz
  类型），加载失败是普通错误，只支持 face 0，度量是设计单位按 `size / unitsPerEm` 缩放，advance
  宽度是 harfbuzz 整形后的 26.6 求和。shell 每次运行解析一个字体（`--font` 文件或 fontconfig 对
  `--font-family` 的最佳匹配，未知字体族回退），并共享给布局度量与绘制；整个会话一个 face（暂不
  支持按元素的 CSS 字体选择）。fontconfig 是 shell 独有的依赖；freetype/harfbuzz 是 `render`
  私有实现。
- 图片解码契约（`render::DecodePng`）：只支持 PNG，经由接口后的已批准 libpng 依赖，输出直通 alpha 的
  ARGB `render::Image`；接受尺寸为每边最多 8192 px 且总计最多 16,777,216 像素（包含式上界），在分配前
  拒绝；失败返回 false 并给出可读错误、`*out` 不变；不抛异常；公开头文件中不出现 libpng 类型。
- HTTPS/TLS 契约（M9）：URL scheme 集合为 `file`、`http`、`https`（小写）；`https` 默认端口 443，
  与 http 相同的 RFC 3986 authority/端口/path 规则，`ParseUrl`/`ResolveUrl`/`SerializeUrl` 可往返。
  `Fetch` 按 scheme 分派；http 与 https 通过私有的字节流抽象（`src/net/stream_internal.h`）共享同一套
  HTTP/1.1 请求/响应实现，纯 TCP 传输保持 M9 之前可观察的行为与输出。TLS 使用 OpenSSL（PRIVATE
  链接到 `toy_net`；公开头文件中没有 OpenSSL 类型）：进程级 `SSL_CTX` 在第一次 https 抓取时惰性创建，
  来自 OpenSSL 默认校验路径（因此 `SSL_CERT_FILE`/`SSL_CERT_DIR` 在该时刻生效），`SSL_VERIFY_PEER`
  始终开启，DNS 主机发送 SNI（IPv4 字面量不发送），证书链 + 主机名校验（DNS 用 `SSL_set1_host`，
  IPv4 字面量用 IP SAN 校验），没有 insecure 模式或关闭校验的途径。TCP 连接用
  `connect_timeout_ms`；TLS 握手与所有 TLS 读写用 `read_timeout_ms`。失败是普通 fetch 错误且文本
  可读：超时含 "timed out"，证书校验失败含 "certificate"，主机名不匹配含 "hostname"。干净的
  `close_notify` 与非干净 EOF 都视为流结束（截断仍由 Content-Length/chunked 捕获）。TLS 路径在进程内
  第一次使用时忽略一次 `SIGPIPE`；库代码不向 stdout/stderr 打印任何东西。重定向 `Location` 用
  `net::ResolveUrl` 解析（绝对、相对、根相对、协议相对、scheme 相对），允许 http 与 https 互切，拒绝
  `file` 目标；`max_redirects` 不变。不做 ALPN/HTTP2、压缩、cookie、HSTS、客户端证书、TLS 会话复用
  或 HTTP 缓存。shell 无需改动 API 或 CLI：页面抓取、链接、重定向与图片都复用同样的 `net` 调用。
  TLS 测试从不访问公网：提交的仅测试用证书（`tests/fixtures/tls/`）、进程内 TLS loopback 服务器
  （`tests/support/tls_server.h`），信任仅通过 CTest `SSL_CERT_FILE` 环境变量指向测试 CA 提供。
- Shell 页面加载器契约（`src/shell/page.h`，私有）：抓取/解析/样式之后，按文档顺序（迭代遍历）加载每个
  `<img>`，取第一个 `src`，相对页面最终 URL 解析，用 `net::Fetch`（http/https/file）抓取并解码，维护按
  img DOM 节点 id 索引的 `layout::ReplacedSizes` + `paint::ImageMap`。缺失/失败/不支持的图片被静默跳过
  （没有 broken-image UI、没有 stderr、页面加载仍成功，元素按空行内布局）；没有图片缓存（每次出现各
  抓取一次），即使元素不可见也会抓取。`--dump-boxes` 使用同一加载器；`--dump-dom`/`--dump-style`
  不抓取图片。
- 导航/会话契约（`toy/shell/browser.h`）：每次成功加载一条历史，加载失败不改变任何东西，每次
  成功加载把滚动偏移重置为 0，只改变 fragment 的引用是 no-op，标题是第一个 `<title>` 文本
  （折叠空白，没有时回退到序列化最终 URL），只有 `http`/`https`/`file` 引用会导航。M8a 保持公开方法完全
  不变，但用前向声明、out-of-line 析构函数与 `std::unique_ptr` 隐藏页面（DOM/样式/盒树/图片），
  因此头文件不再暴露页面数据，`Browser` 不可拷贝。M8b 只增加可选的 `font` 构造函数参数
  （`Browser(w, h, std::shared_ptr<const render::Font> = nullptr)`）；null 表示位图路径，加载的
  字体会用于该会话的每次加载/重新布局/绘制。
- 平台输入契约（`toy/platform/window.h`）：`kMouseDown` 是窗口设备像素中的主键点击，`kMouseWheel`
  携带 `wheel_delta`（正值表示朝文档末尾滚动），`kKeyDown` 携带归一化的 `Key` 与 alt/ctrl/shift；
  `SetTitle` 尽力而为，不会让窗口失败。
- [architecture.md](architecture.md) 中的模块边界与依赖方向。
- `--size` 每边被钳制在 1..8192，且总像素最多 16M；解码图片使用相同的包含式上界（每边最多 8192 px，总计最多 16,777,216 像素）；body 由 `FetchOptions::max_body_bytes` 限制（默认 16 MiB）；dump 缩进最多 64 层。
- HTML/CSS/JS 支持是刻意不完整的：读 `roadmap.md` 里的 known-limits 条目，而不是假设规范行为。

## 新团队如何开始工作

1. 读本文件、[docs/README.md](README.md)，然后读 `AGENTS.md`。
2. 运行上面的验证命令；如果结果与本快照不同，把它当作 bug，修复文档或代码。
3. 从 `roadmap.md` 选择下一个里程碑；按 [team-workflow.md](team-workflow.md) 走一轮的流程：intake → contracts → kickoff → parallel work → integration → delivery → retro。
4. Lead 拥有文档、共享 CMake 文件与提交；实现者认领一个任务、留在其写范围内、本地验证并汇报。
5. 在添加任何依赖、下载代码/测试数据或更改 [decisions.md](decisions.md) 中的已锁定决策之前，先询问用户。

## 曾让上一支团队付出时间的坑

- 能用 `--dump-*` 模式证明同一件事时，不要用显示器测试；一切设计都以 headless 优先。
- 深层嵌套输入曾让 dump 变成二次复杂度；任何新的 dump 都要保持有深度上限并有测试。布局是刻意做成迭代式的（10,000 层嵌套 div 是一个测试）：不要把它改回 DOM 递归。
- 构建产物与目标文件绝不能提交；使用隔离的构建目录。
- `https` 自 M9 起可用（`http`/`https`/`file`）；上面的 M9 TLS 契约已冻结。校验始终开启：绝不加入
  insecure 模式、“跳过校验”标志或访问公网的测试。
- TLS 测试依赖两个必须保持仅测试用的部件：`tests/fixtures/tls/` 中提交的证书（CA、`localhost` 叶证书
  与无关的不受信任叶证书；2046 年过期）与 header-only 的进程内服务器
  `tests/support/tls_server.h`。信任只通过 CTest `ENVIRONMENT` 把 `SSL_CERT_FILE` 指向测试 CA 提供——
  不要在测试里 `setenv` 覆盖，也不要使用已安装/系统 CA。
- 进程级 `SSL_CTX` 在第一次 `https://` 抓取时惰性创建，因此默认校验路径（含
  `SSL_CERT_FILE`/`SSL_CERT_DIR`）在该时刻读取，同一进程后续修改会被忽略。这就是用 CTest 环境变量
  建立信任的原因；测试不能中途切换信任库。
- TLS 路径会安装一次进程级 `SIGPIPE` 忽略，因为 OpenSSL 的写路径可能触发它；纯 HTTP 路径使用
  `MSG_NOSIGNAL`。在没有更好机制之前不要移除二者。
- OpenSSL 失败必须保持为带可读文本的普通 `Fetch` 错误（测试断言 "timed out"、"certificate"、
  "hostname" 子串）：绝不让 OpenSSL 类型或异常进入公开头文件或调用方，库代码绝不打印。
- `SSL_CTX_set_default_verify_paths` 加 `SSL_VERIFY_PEER` 就是全部信任策略：没有证书 pinning、主机
  白名单、HSTS 或客户端证书，也没有 HTTP/2/ALPN、压缩、cookie 或代理。需要这些特性的真实站点会失败
  或显示不完整——这是已记录的 M9 限制，不是 bug。
- 重定向现在走 `net::ResolveUrl`，因此相对、协议相对与 scheme 相对的 `Location` 都会被跟随，允许
  http<->https 切换，拒绝 `file` 目标。M9 之前“相对 Location = 错误”的行为已被有意移除。
- 导航 fixture 的 “Unsupported page” 链接故意使用 `ftp://example.com/`，这样在 https 可导航之后，
  不支持 scheme 的点击用例不会访问网络；不要改回 https 链接。导航测试仍然硬编码该链接的坐标。
- M9 与 M8b 有同样的字节兼容门禁：所有 M9 之前的 http/file 输入 dump 与截图必须与 M8b 基线
  `cmp` 相同，GCC/Clang 的 PPM 必须逐字节相同。改动 `net`/绘制后要重跑 `git worktree` 基线。
- 真实站点检查是手动的：公网 `https://` 页面受限于现有 HTML/CSS 子集，看起来不完整；这是预期行为。
  不要把下载的页面内容、截图、代理/网络细节或站点专属 fixture 提交进仓库。
- HTML/CSS/URL 解析器按设计是容错的；它们遇到畸形输入永不失败，所以“没有报错”不是正确性的证据——把 dump 与 golden 测试比较。
- `--dump-boxes` 用 `--size` 的宽度作为视口并忽略高度；高度由内容决定。`layout` 绝不能包含 `render` 头文件——文本度量只通过回调到达。
- 绘制刻意使用整数像素、不做抗锯齿：改变取整、字形缩放或绘制顺序都会改变 golden 像素测试。GCC 与 Clang 必须产出逐字节相同的 PPM——用 `cmp` 比较两套构建的截图是很便宜的确定性门。
- 绘制顺序在文档中是前序的“先背景、再子节点”；还没有层叠上下文，所以未经契约改动不要加入 alpha/重叠语义。
- canvas 由 `shell` 填白；`html`/`body` 的 background-color 只绘制其元素盒（不会传播为 canvas 背景）——这是刻意的 M6 限制，不是 bug。
- 光栅化器的工作量必须始终受裁剪/surface 区域限制：对抗性字号曾让旧的字形循环挂起或溢出；修改 `render::DrawDisplayList` 时保留有限性守卫、可见范围迭代与有界单元数学。
- 临时文件（fixture、截图、笔记、下载）放在仓库根目录被 gitignore 的 `.tmp/`，不要散落在树中。
- 鼠标/键盘行为无法 headless 单测。真实窗口冒烟测试会在显示器上启动浏览器，用
  `xwininfo -root -tree` 找到*客户端*窗口（窗口管理器边框与客户端都带标题；截取尺寸精确的客户端），
  用 ImageMagick `import -window <id>` 截图，并用一个 `.tmp` 里的一次性 XSendEvent 小工具注入
  点击/滚轮/按键（只链 libX11，不提交）。改动事件循环后要重跑。
- 点击坐标是窗口设备像素：先加上滚动偏移再做 `layout::HitTest`。命中测试返回前序中最后一个盒，
  它可能是匿名盒或文本盒，所以 `shell` 先沿盒父链找到 DOM 来源，再沿 DOM 祖先找 `href`。
- 导航测试硬编码 fixture 的链接坐标（来自 `toybrowser --size 400x200 --dump-boxes` 一次运行）；
  改动 fixture 或布局常量就意味着要更新这些坐标与预期内容高度。
- `paint` 现在总会用视口裁剪对包裹非空页面；以往“M6 不发出裁剪命令”的说法已过时。裁剪不会改变
  滚动 0 的像素。
- 每次成功加载后滚动都重置为 0、纯 fragment 链接是 no-op，这两点是有文档的 M7 限制；未经契约变更
  不要“修复”它们。
- 窗口循环在 expose、resize 与滚动时重建布局与显示列表。这是正确且刻意简单的 MVP 实现；只有在有
  证据时才优化。
- 内置 8x16 字形表是手写的，字形形状靠肉眼复核：刻意不保留冻结的字形位图/指纹测试。改字形就是
  视觉改动——提交前渲染一个样例看一眼。
- libpng 现在是必需的构建依赖（`src/render/CMakeLists.txt` 中的 `find_package(PNG REQUIRED)`）；
  缺少开发头文件的机器会在 configure 阶段失败。不要硬编码版本或路径，也不要把 `png.h` 放进公开头文件。
- PNG 解码只经由 `render::DecodePng`。尺寸在分配前校验，输出是直通 alpha 的 ARGB，失败是普通错误。
  `tests/fixtures/images/` 的手工 fixtures 文档化了精确像素内容；fixture 若改动，README 与测试要同步。
- 无图片的 dump 与截图必须与 M7 逐字节一致，图片 PPM 在 GCC 与 Clang 之间必须相同：任何触碰绘制的
  轮次结束时，用 `git worktree` 取上一提交的新鲜构建做 `cmp`。这个 M7 门禁已经多次便宜地抓到漂移。
- 布局与绘制必须用相同的 DOM 节点 id 关联图片（`layout::ReplacedSizes` 与 `paint::ImageMap`）；
  `shell` 从一次加载同时构建两者。`RelayoutPage` 复用已存尺寸与解码图片，resize 时从不重新抓取。
- 显示列表的图片命令持有 `shared_ptr<const render::Image>`；构建出的列表可能比页面活得更久，因此命令
  里绝不能存裸图片指针。
- 图片测试（`shell.images*` 以及 `tests/fixtures/img_site/` 的 dump 正则）硬编码了来自
  `toybrowser --size 320x200 --dump-boxes` 的几何，和导航测试硬编码链接坐标一样：改 fixture、
  replaced 元素布局或文本度量时，要在同一改动里更新这些常量。
- `shell.images_screenshot` 是 `cmake -P` 辅助脚本，只检查退出状态、P6 头与文件大小；同一页面的像素
  内容由 `shell.images` 断言。
- `--dump-boxes` 现在也会加载并解码页面图片（`<img>` 盒就是这样出现的）；图片失败仍被跳过，因此坏
  图片不会让 dump 失败。
- 图片加载没有缓存也没有懒加载：每个 `<img>` 出现都会被抓取，即使它被 `display:none` 隐藏或远在
  视口之外。这是有文档的 M8a MVP 限制，不是 bug；加入缓存/懒加载需要契约变更。
- M8b 增加了硬性的逐字节兼容门禁：不给 `--font`/`--font-family` 时，所有 M8b 之前的 dump 与截图
  必须与 M8a 基线 `cmp` 一致；real-font 路径完全 opt-in。任何触碰绘制/字体的改动后都要用上一提交的
  `git worktree` 基线重跑。
- real-font 测试必须使用提交的自造 fixture `tests/fixtures/fonts/toy_test.ttf`，从不使用已安装字体；
  fontconfig CLI 测试把 `FONTCONFIG_FILE` 指向生成配置，其唯一 `<dir>` 是 fixture 字体目录。已安装
  字体只用于手工演示（任意 fontconfig 字体族都可用，例如 `sans-serif`）。
- 不要在测试里断言精确的 freetype 覆盖率字节：抗锯齿像素依赖库版本。`render.paint` 断言着墨区域、
  AA 存在性、裁剪边界、no-op 与确定性；`shell.fonts` 断言页面区域与冻结的布局几何。GCC 与 Clang
  在同一机器/库版本下仍必须产出逐字节相同的 PPM。
- real-font 有效字号在度量、整形与光栅化中都被钳制到 `kMaxRealFontSizePx`（4096 px）。超过该值的
  字号行为与 4096 完全相同，因此恶意 CSS `font-size` 无法让 freetype 分配无界字形位图；位图路径
  保留自己的守卫。
- `Font` 在度量/绘制调用之间会修改共享的 freetype face 字号：绝不要跨线程共享同一个 `Font`
  （MVP 单线程）。`DrawTextWithFont` 传入 null font 时不追加命令；位图路径是 `DrawText` + 空 font、
  cell 顶部 y。
- shell 的字体 fixture 几何（`shell.fonts*` 的 dump 正则）硬编码自
  `toybrowser --font tests/fixtures/fonts/toy_test.ttf --size 320x200 --dump-boxes`；改 fixture、
  其度量或字体流水线时要在同一改动里更新这些常量，和 M7/M8a fixture 几何一样。
- `--font-family` 使用 fontconfig 的正常语义：未知字体族返回最佳匹配而不报错，使用匹配文件的
  face 0，忽略 TTC/face index。M8b MVP 每会话只支持一个字体；按元素的 CSS
  `font-family`/weight/style 是后续工作。
- `src/render/CMakeLists.txt` 现在 `find_package` freetype（模块）与 harfbuzz（CONFIG，目标
  `harfbuzz::harfbuzz`）；`src/shell/CMakeLists.txt` 查找 fontconfig（目标
  `Fontconfig::Fontconfig`）。全部保持 PRIVATE，不硬编码版本/路径。
- `src/net/CMakeLists.txt` 用 PRIVATE 查找 OpenSSL（`OpenSSL::SSL` + `OpenSSL::Crypto`）；
  `net.tls`/`net.http`/`shell.https` 测试可执行文件也链接 OpenSSL，因为它们要编译进程内测试服务器；
  它们传入的 fixture 路径来自 `CMAKE_CURRENT_SOURCE_DIR`（由 CMake 生成，绝不硬编码）。公开头文件保持
  不含 OpenSSL，也不要添加版本、路径或 vendoring。
