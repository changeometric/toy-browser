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
| M10 | 自动字体选择：CSS `font-family`/`font-weight`/`font-style` 通过已批准的 fontconfig 匹配，初始字体族 `serif`，按码点字符覆盖切分 run 实现回退（拉丁页面上的汉字，反之亦然），根 `<html lang>` 作为 fontconfig 语言提示；共享的 `shell::Fonts` 对象同时驱动布局度量与绘制（`TextMetrics::ascent_px`、`Box::baseline_offset_px`、`paint::FontRunsProvider`）；layout 中加入简化的 CJK 断行单元，使中文段落可换行；提交了常规/粗体/斜体/汉字 fixture 字体与确定性的 fixture/空 fontconfig 测试配置；保留 `--font`/`--font-family` 覆盖；位图路径仍是逐字节相同的退化路径；66 个测试 |
| M11 | 性能：默认 RelWithDebInfo 构建 + `-ffp-contract=off`；render 文本缓存（每个字体复用一个 hb_font/hb_buffer，有界 shaped-run 与 glyph 覆盖率 LRU）；shell 字体缓存（无分配的解析样式快速路径、度量与 run LRU）；paint 视口裁剪（文本保守余量）；窗口循环修正（Expose 不重排、resize 仅尺寸变化时重排、合并 X11 Expose/ConfigureNotify burst、复用 surface）；渲染逐字节不变；66 个测试 |
| 审阅轮 | 修复无上限的 `--size`、文件 body 上限、dump 缩进；20 个 CTest 测试 |

**下一计划里程碑：M12 —— HTTP 代理（已计划，尚未开始；用户已降低优先级）。** M11（性能）已
交付。M12 通过 `FetchOptions` 支持 HTTP 代理（`http` 用 absolute-form，`https`
用 `CONNECT`）；用户表示代理目前不重要，后续工作按其优先级安排。JavaScript 子集已暂缓。

尚未实现：代理（计划 M11）、JavaScript（已暂缓）、外部样式表、border、水平滚动/滚动条、
hover/active 链接样式与表单控件，以及 `@font-face`/webfont 下载。字体选择自 M10 起自动完成
（CSS 字体族/weight/style 经 fontconfig、字符覆盖回退、CJK 断行）；其他语言尽力支持，bidi/RTL 与
完整 Unicode 断行尚未实现。PNG 图片自 M8a 起可用、真实字体自 M8b 起可用、`https://` 自 M9 起可用
（已知限制见 [roadmap.md](roadmap.md)）；`http://` 与 `file://` 仍受支持。窗口与 `--screenshot` 在
给出 URL 时绘制真实页面，否则显示 M1 占位页；给出页面 URL 时窗口也可浏览（链接、滚轮/键盘滚动、
Alt+Left/Right 历史、`<title>`）。`--dump-*` 模式仍是主要调试路径；使用系统字体时文本输出按设计
依赖机器（测试通过 `FONTCONFIG_FILE` 固定）。

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

预期：66/66 测试通过；使用 `-DTOYBROWSER_WERROR=ON` 构建在 GCC 和 Clang 上无警告；dump 输出确定性的树；截图写出二进制 PPM（带 URL 的形式绘制真实页面，不带 URL 的形式保留占位页）。所有 headless 模式都不需要显示器。需要 CMake 3.20 或更新版本；构建需要 X11、libpng、freetype、harfbuzz、fontconfig 与 OpenSSL 开发头文件（均为已批准的依赖）；只有窗口模式需要运行中的显示服务器。未指定 `CMAKE_BUILD_TYPE` 时默认 `RelWithDebInfo`（M11）；调试可传 `-DCMAKE_BUILD_TYPE=Debug`。`-ffp-contract=off` 是刻意加的，保证布局几何在不同构建类型与编译器间逐字节一致。`CMakePresets.json` 把同样的命令封装为 `default`/`dev` presets，`.github/workflows/ci.yml` 用 GCC 与 Clang 运行 configure、build、test 与安装冒烟步骤。

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
`<img> inline` 盒；`missing.png` 不保留盒。`--font` fixture 的盒 dump 保持 M8b 度量（例如 32 px 下
`<h1>` 文本 "ABC" 宽 58）。`--font-family` 现在遵循计算出的 weight/style，因此 UA 的粗体 h1 使用粗体
face（`shell.fonts_dump_boxes_family` 固定该行为）。不传字体标志时页面使用自动系统字体（按设计依赖
机器）；测试通过 `FONTCONFIG_FILE` 固定为确定性的 fixture/空配置，空配置让所有 M10 之前的 golden
保持内置位图输出。

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

```sh
# 字体选择（M10）：使用提交的 fixture 字体与测试 fontconfig 配置进行确定性测试；中文 fixture 页面
# 从磁盘（file://）加载，用 fixture 汉字 face 与逐字符回退 run 布局/绘制。
ctest --test-dir "$BUILD" -R '^(style\.parser|layout\.baseline|layout\.cjk_line_break|shell\.font_selection|shell\.page_language|shell\.cjk_.*|shell\.fonts.*)$' --output-on-failure

# 手工系统字体检查：完全不传字体标志。文本使用已安装字体（默认 serif、weight/style、逐字符回退），
# 依赖机器。中文站点用汉字回退 face 渲染，并且因为 CJK 断行单元而正常换行。
"$BUILD"/toybrowser --size 800x900 --screenshot /tmp/toybrowser-zh.ppm \
    "$PWD/tests/fixtures/cjk_site/index.html"
"$BUILD"/toybrowser --size 800x600 --dump-boxes "$PWD/tests/fixtures/nav_site/index.html" | grep 'text "'
```

预期：所有字体测试确定性地通过（fixture 配置：默认 serif、粗体/斜体 face、混合拉丁+汉字 run、提交的
中文页面逐单元文本盒、换行与绘制像素；空配置：逐字节相同的位图路径）。手工页面显示真实系统字体，
中文可读且正常换行。

窗口演示（需要显示器）：

```sh
"$BUILD"/toybrowser --size 320x600 "file://$PWD/tests/fixtures/img_site/index.html"
"$BUILD"/toybrowser --font-family "sans-serif" --size 400x200 \
    "file://$PWD/tests/fixtures/nav_site/index.html"
# 图片页：确认 PNG 图片被绘制；真实字体页：确认抗锯齿文本；导航页：点击 "About page"，用滚轮/
# 方向键/PageUp/PageDown/Home/End 滚动，Alt+Left/Right 前进后退；窗口标题跟随 <title>。
```

## 下一轮：M12 —— HTTP 代理（已计划；等用户确认后开始）

**状态：** 只有设计草案（见 [roadmap.md](roadmap.md)）；还没有 M11 代码。M10 已完成且全绿，因此
起点就是当前 HEAD；用户降低了代理优先级，开始前先确认。

- 目标：`http://` 通过 HTTP 代理以 absolute-form 请求目标抓取；`https://` 通过代理 `CONNECT` 隧道后
  再做正常的 TLS 握手。浏览器自身的代理支持是在受限网络上让真实站点测试变得实用的能力。
- 预期形态（契约在该轮开始时冻结）：`--proxy host:port` 加标准 `http_proxy`/`https_proxy` 环境变量
  回退，全部留在 `net` 内、位于 `FetchOptions` 之后；预计不需要新的第三方依赖；进程内假代理测试
  （不访问公网），未配置代理时输出逐字节不变。
- 在重新划范围之前不做：SOCKS 代理、代理认证、bypass 列表、PAC。

M11 之后总路线图就全部变绿；剩余工作是已记录的 backlog（webfont、更多图片格式、外部样式表、
bidi/RTL 与完整 Unicode 断行等）以及用户的新优先级。

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
  `toy/render/paint.h`，`src/paint/` 从盒树构建列表且不光栅化（M11：`src/paint/paint.cpp` 的
  视口裁剪）。字体选择位于 `src/shell/fonts.h`/`fonts.cpp`（`Fonts` pimpl：fontconfig 匹配、run
  回退、M11 私有解析样式/度量/run 缓存），它是唯一使用 fontconfig 的地方；`src/shell/application.cpp`
  每次运行根据 `--font`/`--font-family` 或自动系统默认构建一个 `Fonts`。`src/render/font_ttf.cpp`
  含 M11 的 shaped-run/glyph 覆盖率缓存与私有 `src/render/font_cache_internal.h` LRU；`src/platform/window.cpp`
  合并 X11 Expose/ConfigureNotify burst（M11）。`layout` 通过 `shell` 提供的回调消费文本度量，自身
  不依赖 `render`；`src/layout/text_break.cpp` 拥有断行单元分词器（单词加 CJK 单元与标点禁则）。
- `tests/<module>/` —— 每个主题一个可执行文件，注册到 CTest；新测试使用 header-only 的
  `tests/support/check.h`。提交的 fixtures：`tests/fixtures/nav_site/`（三页导航演示；它的
  “Unsupported page” 链接是故意的不支持 scheme 用例，任何测试都不会访问公网）、
  `tests/fixtures/img_site/`（图片流水线演示）、`tests/fixtures/font_site/`（真实字体 + 图片演示）、
  `tests/fixtures/images/`（像素内容有文档的手工 PNG）、`tests/fixtures/fonts/`（自造最小 TTF：
  常规、粗体、斜体与一个小汉字 face，度量/字体族有文档；测试从不使用已安装字体）、
  `tests/fixtures/cjk_site/`（从磁盘加载的中文页面，断言逐单元文本盒、换行与绘制像素）、
  `tests/fixtures/https_site/`（shell 测试通过 TLS 提供的 HTML+PNG 页面）与 `tests/fixtures/tls/`（提交的仅测试用 CA、`localhost` 叶证书与不受信任叶
  证书/私钥；重新生成方法见其 README）。二进制 fixture 的生成器与 fixture 一起提交：
  `tests/fixtures/fonts/generate_toy_test.py`、`tests/fixtures/fonts/generate_variants.py` 与
  `tests/fixtures/images/generate.py`（只用标准库、输出确定；见各 fixture README）。
  `tests/support/tls_server.h` 是 `net.tls`/`net.http`/`shell.https` 共用的 header-only 进程内 TLS
  loopback 服务器；没有任何测试访问公网。
- 根目录公开文件 —— `README.md`/`README.zh.md`（面向使用者的入口）、`CONTRIBUTING.md`、
  `SECURITY.md`、`CMakePresets.json`、`.github/workflows/ci.yml` 与 `docs/assets/` 截图。
- `docs/` —— 架构、路线图、约定、团队流程、决策、依赖、待决问题（本文件是快照）。
- 共享任务板 —— 由 agent 运行时提供（任务 create/claim/update，带 revision）；如果运行时没有，则用 `docs/tasks/<milestone>.md`，字段相同。见 [team-workflow.md](team-workflow.md)。
- `README.md` / `README.zh.md` 与 `docs/zh/` —— 面向用户的入口（英语为权威）。
- `.tmp/` —— 被 gitignore 的本地暂存区，用于笔记、fixture、下载与测试输出。

## 冻结契约（不要随意改动）

- CLI 模式、标志与退出码：`0` 成功，`1` 运行时错误，`2` 用法错误。可选的位置参数页面 URL（`toybrowser [options] [url]`）只用于窗口模式和 `--screenshot`；最多一个 URL，不能与 `--dump-*` 模式组合，不给 URL 时保留 M1 占位页。M8b 新增 `--font PATH` 与 `--font-family NAME`：各自需要非空参数，二者互斥，且与 `--dump-body`/`--dump-dom`/`--dump-style` 组合是用法错误（退出码 2）；允许与窗口模式、`--screenshot`、`--dump-boxes` 组合。选中的字体无法加载/解析是运行时错误（退出码 1）。两个标志都不给时浏览器使用自动系统字体（M10：计算出的 CSS 字体族/weight/style 经 fontconfig，带字符回退）；只有没有可用系统字体时才退化到内置位图字体。`--font PATH` 保持单一精确 face、无回退；`--font-family NAME` 强制首选字体族并保留字符回退，且自 M10 起也遵循计算出的 weight/style。
- Dump 格式（DOM、style、boxes）：确定性、面向行、由测试断言；任何改动都需要同一轮内更新测试和文档。style dump 自 M10 起在 `font-style=` 之后有 `font-family=` 字段：项之间用 ", " 连接，匹配 `[A-Za-z0-9_-]+` 的项原样输出，否则用双引号包裹并以反斜杠转义 `\` 与 `"`。盒 dump 是前序遍历的行：`<tag> block|inline`、`anonymous block` 或 `text "..."`，后接 `x= y= width= height=`，全部是绝对 CSS 像素取整后的整数（`std::lround`）；缩进为每层盒深度两个空格，上限 64 层，格式在 M10/M11 均未改变（CJK 文本只是每个断行单元一个文本盒）。
- 布局语义通过布局测试与 M5/M8a/M10 的 known-limits 冻结：视口宽度来自 `--size`，没有 margin 合并，行顶部对齐，文本盒以单词为单位，换行发生在空白处以及 M10 的 CJK 断行单元（每个 CJK 字符一个单元、简化标点禁则；拉丁词永不拆开、没有完整 UAX#14/词典断行）。自 M10 起 `MeasureText` 回调也收到文本的 `style::ComputedStyle`；`TextMetrics` 携带 `ascent_px`，每个文本盒携带 `baseline_offset_px` = 行最大 ascent（只是元数据，不参与 dump）。M8a replaced 元素：`LayoutOptions::replaced_sizes`（DOM 节点 id → 固有 CSS px）把行内元素变成原子行内级盒；CSS `width`/`height` px 覆盖固有尺寸，只指定一侧时保持固有宽高比，盒整体换行、抬高行高并顶部对齐；其 dump 行仍是 `<tag> inline x= y= width= height=`，没有子节点，`display:block` 的 replaced 元素不支持，replaced 盒的 margin/padding 被忽略。没有条目的页面保持原有几何逐字节不变。
- 绘制语义通过 M6/M7 测试冻结：显示列表命令使用整数设备像素（`FillRect`、`DrawText`、`DrawImage`、`PushClip`/`PopClip`）；填充对不透明目标使用 SrcOver 公式 `(src * a + dst * (255 - a) + 127) / 255`；初始裁剪是整个 surface，光栅化工作量受裁剪限制；`shell` 把 canvas 填白；绘制顺序是盒树前序（先背景、再图片、再子节点/文本）；文本使用内置 8x16 ASCII 字形，最近邻缩放、无抗锯齿、非 ASCII 用 `?`、文本像素不透明。
  M7 绘制：非空页面恰好被一个 `PushClip(0, 0, viewport)`/`PopClip` 对包裹，每条命令的 y 按
  `-scroll_y`（整数设备像素，x 不变）平移；没有图片的页面在滚动 0 必须与 M6/M7 逐字节一致。
  M8a 图片：节点 id 在 `paint::ImageMap` 中的行内 replaced 盒各发出一条 `DrawImage`，在该盒背景之后；
  命令持有 `shared_ptr<const render::Image>`；目标像素 (dx, dy) 用整数向下取整采样源
  `((dx - x) * image.width / width, (dy - y) * image.height / height)`（最近邻，无插值/抗锯齿），
  用同一 SrcOver 公式混合（alpha 0 无操作，alpha 255 写 `0xFF000000 | rgb`，结果不透明）；空图片映射
  精确复现 M8a 之前的命令列表。M10 real-font 文本：paint 接收 `paint::FontRunsProvider`；对每个
  文本盒请求有序、非空、精确拼接成盒文本的 run。每个 run 变成一条 `DrawTextWithFont` 命令，x =
  `round(pen_x)`（pen 从 box.x 开始，按 `font->MeasureTextWidth(run.text, style.font_size_px)` CSS px
  推进），y = `round(box.y) - scroll_y + round(box.baseline_offset_px)`；字体按共享引用保留，字形由
  harfbuzz 整形、freetype 光栅化为 8-bit 覆盖率，按冻结的逐字形整数公式放置（26.6 pen 四舍五入到
  设备像素、远离零，再加 freetype bitmap_left/top），用同一 SrcOver 公式以覆盖率为 alpha 混合
  （结果不透明），裁剪到当前 clip，有效字号钳制到 `kMaxRealFontSizePx`（4096 px）。provider 为
  null/空，或某文本盒返回空 run 时，精确输出 M10 之前的冻结位图命令（`DrawText`、cell 顶部 y、
  不透明无抗锯齿像素），必须逐字节复现。 M11 裁剪：滚动平移后背景 `FillRect` 与 `DrawImage` 的精确设备像素目标矩形与视口不相交时
  省略该命令；文本盒在其矩形四周扩大 `ceil(2 * font_size_px)` 后与视口不相交时省略。裁剪发生在
  样式/图片/字体 run 查找之前，非空盒树仍恰好保留一个 `PushClip`/`PopClip` viewport 对并保持命令
  顺序；被省略的命令本来就不会画出任何像素，因此渲染像素与不裁剪逐字节相同。paint 的显示列表测试
  现在断言这一裁剪契约（显示列表可以合法地省略永不可见命令）；像素仍是冻结契约。
- Render 缓存契约（M11）：`render::Font` 保留私有、单线程、有界 LRU 缓存——每个字体复用一个
  `hb_font`/`hb_buffer`（单一 `hb_face`，因此 harfbuzz shape-plan 缓存生效；freetype face 尺寸变化
  后用 `hb_ft_font_changed` 同步），shaped-run 缓存（精确 26.6 字号 + UTF-8 文本，16 MiB / 16384
  entries），glyph 覆盖率缓存（glyph id + 26.6 字号，32 MiB / 16384 entries，负结果也缓存）。缓存
  分配失败视为 miss；逐出不改变像素、度量或 advance；渲染公式、clip、SrcOver 与位图字体路径不变；
  不改任何公开头文件/API。
- 字体引擎契约（M8b）：`render::Font` 加载后不可变（pimpl，公开头文件不出现 freetype/harfbuzz
  类型），加载失败是普通错误，只支持 face 0，度量是设计单位按 `size / unitsPerEm` 缩放，advance
  宽度是 harfbuzz 整形后的 26.6 求和。
- 字体选择契约（M10）：`toy/shell/fonts.h` 的 `Fonts`（pimpl）每会话创建一次，模式为
  `SystemDefault`、`FromFile` 或 `FromFamily`。`SystemDefault` 把计算出的
  `font_family`（初始 `serif`）/`font_weight`/`font_style` 加页面语言提示（根 `<html lang>`）交给
  fontconfig，按码点字符覆盖把一个文本盒的 UTF-8 切成 run（第一个字符集覆盖它的候选；没有候选覆盖
  的码点用主 face 的 `.notdef`），按（字体族列表、weight、slant、language）缓存候选列表、按文件缓存
  face，并由布局度量适配器与 paint run provider 共享，因此度量与绘制始终一致。`FromFile` 是单一
  精确 face（无回退）；`FromFamily` 强制首选字体族并保留字符回退，不可用时返回可读错误。没有可用
  系统字体（fontconfig 初始化失败、无候选、无可加载文件）时 `SystemDefault` 进入位图模式：位图
  度量与空 run，页面仍能加载。fontconfig 仍是 shell 独有；freetype/harfbuzz 仍是 `render` 私有实现；
  默认浏览器行为与自动选择都是 M10 才引入的。
- 字体确定性契约（M10）：测试从不使用已安装字体或机器字体配置。通过 CTest `ENVIRONMENT` 把
  `FONTCONFIG_FILE` 指向两个生成配置之一：fixture 配置（`<dir>` = `tests/fixtures/fonts/`，通用族
  映射到 `Toy Test Font`）用于自动选择/回退测试；空配置（无字体目录）强制位图路径，必须逐字节复现
  所有 M10 之前的 dump/截图。`Fonts` 缓存保证固定 fontconfig 配置与字体集下 run/度量确定；不要添加
  代码内的信任/覆盖后门。
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
  因此头文件不再暴露页面数据，`Browser` 不可拷贝。M10 起可选的构造函数参数是
  `Browser(w, h, std::shared_ptr<const Fonts> = nullptr)`；null 表示位图路径（M10 之前的测试行为），
  `Fonts` 会用于该会话的每次加载/重新布局/绘制。
- 平台输入契约（`toy/platform/window.h`）：`kMouseDown` 是窗口设备像素中的主键点击，`kMouseWheel`
  携带 `wheel_delta`（正值表示朝文档末尾滚动），`kKeyDown` 携带归一化的 `Key` 与 alt/ctrl/shift；
  `SetTitle` 尽力而为，不会让窗口失败。M11 `WaitEvent` 合并语义：后端可以把已排队的连续 Expose
  合并为一个 `kExpose`，把连续 ConfigureNotify 合并为一个携带最终尺寸的 `kResize`（尺寸未变化的
  burst 不产生事件且 WaitEvent 继续阻塞）；输入与 close 事件永不合并、丢弃或乱序，阻塞语义不变。
- [architecture.md](architecture.md) 中的模块边界与依赖方向。
- `--size` 每边被钳制在 1..8192，且总像素最多 16M；解码图片使用相同的包含式上界（每边最多 8192 px，总计最多 16,777,216 像素）；body 由 `FetchOptions::max_body_bytes` 限制（默认 16 MiB）；dump 缩进最多 64 层。
- 构建契约（M11）：未指定 `CMAKE_BUILD_TYPE` 时单配置生成器默认 `RelWithDebInfo`；第一方目标编译时加 `-ffp-contract=off`，使浮点布局几何在不同构建类型与 GCC/Clang 间逐字节一致。不要移除默认构建类型（未优化构建慢 3–7 倍）或 FP 参数，除非重跑全量测试、字节兼容与跨编译器门禁；测试里不要加耗时断言。
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
- M10 通过位图路径保留硬性逐字节兼容门禁：把 `FONTCONFIG_FILE` 指向生成的空配置（无字体）时，
  所有 M10 之前的 dump 与截图必须与 M9 基线 `cmp` 一致；唯一有意的差异是 style dump 新增的
  `font-family=` 字段，门禁在比较前会把它规范化掉。任何触碰绘制/字体/布局的改动后都要用上一提交的
  `git worktree` 基线重跑。
- 字体测试必须使用提交的自造 fixture（`tests/fixtures/fonts/`：常规、粗体、斜体、汉字），从不使用
  已安装字体。自动选择测试把 `FONTCONFIG_FILE` 指向生成的 fixture 配置（只有 fixture 字体，通用族
  映射到 `Toy Test Font`）；位图兼容测试指向空配置。已安装字体只用于手工演示。
  `tests/fixtures/fonts/README.md` 记录了确切的配置配方（含 fontconfig 把 `sans-serif` 规范化为
  `sans` 这一点）。
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
- `--font-family` 为所有文本强制首选字体族（仍保留字符回退），且自 M10 起也遵循计算出的
  `font-weight`/`font-style`，因此 UA 粗体 h1 在 `--font-family` 下也会使用粗体 face（M8b 的期望
  已随之调整）。未知字体族经 fontconfig 回退而不报错；使用匹配文件的 face 0，忽略 TTC/face index。
  `--font PATH` 仍是单一精确 face、无回退。
- `src/render/CMakeLists.txt` 现在 `find_package` freetype（模块）与 harfbuzz（CONFIG，目标
  `harfbuzz::harfbuzz`）；`src/shell/CMakeLists.txt` 查找 fontconfig（目标
  `Fontconfig::Fontconfig`）。全部保持 PRIVATE，不硬编码版本/路径。
- `src/net/CMakeLists.txt` 用 PRIVATE 查找 OpenSSL（`OpenSSL::SSL` + `OpenSSL::Crypto`）；
  `net.tls`/`net.http`/`shell.https` 测试可执行文件也链接 OpenSSL，因为它们要编译进程内测试服务器；
  它们传入的 fixture 路径来自 `CMAKE_CURRENT_SOURCE_DIR`（由 CMake 生成，绝不硬编码）。公开头文件保持
  不含 OpenSSL，也不要添加版本、路径或 vendoring。
- M10 自动字体选择依赖首次使用时创建的进程级 fontconfig 状态：`FONTCONFIG_FILE`/`FONTCONFIG_PATH`
  在 `Fonts` 第一次解析样式时被读取，因此测试必须在进程启动前通过 CTest `ENVIRONMENT` 属性设置它。
  不要在进程中途切换字体配置；不要添加代码内的信任/覆盖后门。
- `Fonts` 按（字体族列表、weight、slant、language）缓存候选 face、按文件路径缓存已加载 face，并被
  布局度量与绘制共享：绝不要跨线程共享一个 `Fonts`（MVP 单线程），也不要为同一页面构建第二个选择，
  否则度量与绘制可能不一致。`--font`/`--font-family` 选择失败是运行时错误（退出码 1）；自动系统
  字体失败退化到位图路径，而不是让页面加载失败。
- layout 的文本分词器（`src/layout/text_break.cpp`）是简化的断行模型：CJK 码点是独立断行单元，
  收尾/起始标点按小规则粘连；没有完整 UAX#14、没有泰语/高棉语/老挝语词典断行、没有连字符断行，
  不同文本节点中的标点无法与前一个单元粘连。拉丁词永不拆开。扩展它需要测试与 roadmap/文档更新。
- 一个 CJK 断行单元对应一个 kText 盒：中文页面会产生很多盒（每字符一个），对 MVP 没问题（受 16 MiB
  body 上限约束），但不要把盒数当成词数写进测试或文档。CJK 页面的盒 dump 天然很大。
- CJK fixtures（`tests/fixtures/cjk_site/`、`layout.cjk_line_break`）冻结逐单元几何与绘制像素；
  `tests/fixtures/fonts/toy_cjk.ttf` 只含合成的方块汉字字形（不是真实字形设计），M10 fixture 字体的
  斜体 face 只有斜体元数据、轮廓未做倾斜。
- 使用系统字体（不传字体标志）时，浏览器文本输出按设计依赖机器：除非固定 `FONTCONFIG_FILE`，dump
  与截图不能跨机器复现。这对真实浏览器是预期的；测试必须始终固定它。
- `paint::BuildDisplayList` 接收 `FontRunsProvider`；空/null provider 仍必须产出冻结的 M10 之前位图
  命令列表（测试会断言）。真实字体 run 必须精确拼成盒文本，并按冻结的 pen/取整规则放置，否则布局
  度量与绘制会不一致。
- fontconfig 是既有已批准依赖；M10 没有新增依赖。它只被 `src/shell/fonts.cpp` 使用；保持
  `fontconfig/*.h` 不出现在任何公开头文件中，也不要添加版本、路径或 vendoring。fixture fontconfig
  配置生成在构建目录（由 `tests/shell/CMakeLists.txt` 生成）；提交的 fixture 字体是测试唯一可用的
  字体文件。
- M11 裁剪意味着显示列表可以合法地省略“绘制区域不可能与视口相交”的命令。像素才是冻结契约；paint
  的显示列表测试同时断言裁剪规则与“与不裁剪 reference 像素相同”。文本余量是
  `ceil(2 * font_size_px)`（四周）；如果未来字体或绘制路径能从盒外更远处着色，需要增大余量并重跑
  裁剪/不裁剪等价测试。
- M11 render 缓存是 `Font` 的私有状态：单线程、有界 LRU（每个 Font 16 MiB shaped runs、32 MiB
  glyph 覆盖率），分配失败视为 miss。不要跨线程共享一个 `Font`（与之前相同），不要加公开缓存 API；
  所有缓存必须输出中性，逐出测试必须比较 fresh 与 warm 结果，绝不能断言耗时。
- M11 shell `Fonts` 缓存同样是私有且有界（16 槽样式快速路径、32 MiB 度量、16 MiB run）。位图退化
  路径刻意绕过它们；缓存绝不能改变度量或 run，`MeasureText`/`Runs` 必须可以任意顺序调用。
- X11 事件合并只合并连续已排队的 Expose/ConfigureNotify 事件；输入与 close 事件永不合并、丢弃或
  乱序。窗口行为无法 headless 单测：改动事件循环后要重跑 M11 真实窗口 smoke（`.tmp` 里的临时
  X11 帮助程序，不提交），验证 resize 后无残留帧、WM_DELETE 干净退出。
- M11 基准工具（`.tmp/perf_stages.cpp`、`.tmp/perf_bench.cpp`、`.tmp/run_m11_bench.sh`、
  `.tmp/gen_baseline_refs.sh`、scroll_shot 工具）刻意不提交：测试不应断言耗时，压力页也只是临时
  fixture。要测量改动时重新创建它们；数值保持机器相对，并在该轮报告里记录 before/after。如果将来
  要提交 benchmark，应做成可选的、不进 CTest 的 target。
- 不要回退默认构建配置：去掉 RelWithDebInfo 默认会让位图路径慢 3–7 倍，去掉 `-ffp-contract=off`
  有跨构建/编译器的像素漂移风险。全量测试、字节兼容门禁与 GCC/Clang PPM 相等是检查手段。
- M11 刻意没有加 GPU 后端：实测填充率不是瓶颈（系统字体光栅化在 800×600 与 1600×1000 下耗时
  相同）；只有在缓存/裁剪之后光栅化仍超帧预算时才重新评估，且 GPU/XShm 工作都算新依赖决策，需要
  用户批准。
- fixture 生成器与它们生成的 fixture 一起提交（`tests/fixtures/fonts/generate_toy_test.py`、
  `tests/fixtures/fonts/generate_variants.py`、`tests/fixtures/images/generate.py`）。字体生成器逐字节
  复现已提交 TTF；PNG 生成器复现有文档的像素内容（精确 PNG 字节可能随本地 zlib 版本变化）。不要手工
  编辑二进制 fixture：改生成器、重新生成、跑全量测试，并在同一改动中更新 fixture README/测试。根
  `.gitignore` 已忽略 `__pycache__`。
