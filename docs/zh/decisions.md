> 本文为中文翻译，英文原版为准：[decisions.md](../decisions.md)。

# 已锁定决策

已经确定、新团队没有真实理由和用户同意不应重新讨论的决策。每条说明决定了什么、为什么，以及细节在哪里。

| 决策 | 原因 | 细节 |
| --- | --- | --- |
| 只用 C++20 与 CMake | 用户要求；现代标准库、单一构建系统 | [AGENTS.md](../../AGENTS.md), `CMakeLists.txt` |
| 目标平台为 Linux 桌面 | 玩具项目的范围控制；不做跨平台抽象 | [AGENTS.md](../../AGENTS.md) |
| 不使用现成浏览器组件（HTML/CSS/JS 引擎、布局引擎、GUI 框架） | 本项目的意义就是自己写它们 | [AGENTS.md](../../AGENTS.md), [dependencies.md](dependencies.md) |
| 任何第三方库都需要用户明确批准 | 保持代码树可移植、项目诚实 | [dependencies.md](dependencies.md) |
| 模块边界：每个模块一个静态库，依赖向下，无环 | 保持模块可测试、可替换 | [architecture.md](architecture.md) |
| DOM 是用整数 `NodeId` 寻址的 arena（区域分配器）；没有持有指针或环 | 生命周期简单、易于 dump、树链接安全 | [architecture.md](architecture.md) |
| MVP 中单进程、单线程、阻塞抓取 | 复杂度预算；只有真正成为问题才异步 | [architecture.md](architecture.md) |
| headless 优先：每个功能不开窗口也能工作并测试 | 支持 CI、调试与自动化验证 | [AGENTS.md](../../AGENTS.md), [architecture.md](architecture.md) |
| 软件光栅化、ARGB32 表面、PPM 截图 | 无 GPU 或图形依赖；测试确定性 | [architecture.md](architecture.md) |
| Dump 是确定性、面向行的契约（`DOM`、style、boxes） | 它们是测试与调试接口 | [architecture.md](architecture.md), [roadmap.md](roadmap.md) |
| `layout` 产出纯 arena `BoxTree`；文本度量通过 `shell` 提供的回调到达（没有 `layout` → `render` 的边） | 保持层边界，并让布局可用假度量器单元测试 | [architecture.md](architecture.md), [roadmap.md](roadmap.md) |
| 绘制使用整数设备像素。自 M10 起正常文本路径是自动选择的真实字体（freetype 8-bit 覆盖率抗锯齿）；没有可用系统字体时退化到内置 8x16 位图字体（最近邻缩放、无抗锯齿），它也是测试固定用的确定性回退路径 | 两条路径都有确定性 fixture 与逐字节相同的 GCC/Clang 截图；空 fontconfig 配置下的位图回退让 M10 之前的输出可逐字节复现 | [architecture.md](architecture.md), [roadmap.md](roadmap.md) |
| `shell` 把页面 canvas 填白；元素 `background-color` 只绘制该元素的盒（不做 canvas 背景传播） | 让 MVP 绘制器显式而简单；真实站点观感不对时再重新考虑 | [architecture.md](architecture.md), [roadmap.md](roadmap.md) |
| 导航状态（当前 URL、历史、滚动偏移）放在 headless 的 `shell::Browser` 中；窗口事件循环与 `--screenshot` 只是其上的薄呈现层 | 让点击、历史、滚动与标题不开窗口也能测试 | [architecture.md](architecture.md), `src/shell/include/toy/shell/browser.h` |
| 点击激活：用滚动后的页面坐标对盒树做命中测试，再沿 DOM 祖先找最近的带 `href` 元素；只有 `http`/`https`/`file` 引用会导航，纯 fragment 引用是 no-op，加载失败保留当前页面、滚动偏移与历史 | 确定性的链接语义，且 `layout`/`render` 不需要了解 HTML | [architecture.md](architecture.md) |
| 滚动只有垂直方向、以整数设备像素表示，范围钳制在 `[0, content_height - viewport_height]`；`paint` 用视口 `PushClip`/`PopClip` 对包裹页面并把每条命令按 `-scroll_y` 平移；导航把偏移重置为 0 | 最小确定性滚动；恢复滚动位置是有文档的 MVP 限制 | [architecture.md](architecture.md), [roadmap.md](roadmap.md) |
| 平台输入归一化（左键点击、滚轮格数、带修饰键的映射按键、`SetTitle`）；`shell` 拥有按键绑定与滚动单位 | 把 X11 细节挡在 `shell` 之外，把页面内容挡在 `platform` 之外 | [architecture.md](architecture.md) |
| M8 使用已批准的底层库而不是自写编解码：libpng + zlib 解 PNG（M8a），freetype 负责字形、harfbuzz 负责整形、fontconfig 负责字体发现（M8b） | 用户于 2026-10-31 决定：“从零”指浏览器功能（网络、HTML/CSS、布局、绘制、JS），不包括底层编解码或字体机制；已批准的库都是宽松许可证，并让 M8 可控 | [dependencies.md](dependencies.md), [roadmap.md](roadmap.md) |
| M8 拆成 M8a（PNG 图片）与 M8b（真实字体）两轮 | 两个更小、可独立审查的轮次；图片与字体相互独立且风险更低 | [roadmap.md](roadmap.md) |
| 图片以整数设备像素的显示列表 `DrawImage` 命令绘制，使用冻结的最近邻源映射与和填充相同的 SrcOver 公式；没有解码图片的页面产出与 M8a 之前完全相同的命令列表 | MVP 中缩放与混合确定、逐字节兼容，不做插值或抗锯齿 | [architecture.md](architecture.md), `src/render/include/toy/render/paint.h` |
| replaced 元素的固有尺寸以调用方提供的、按 DOM 节点 id 索引的映射传给布局；布局从不依赖 render 或图片 | 保持 layout → render 边界，并让布局可用假尺寸测试 | [architecture.md](architecture.md), `src/layout/include/toy/layout/box.h` |
| 手写位图字形表靠肉眼复核；不保留冻结的字形位图/指纹测试 | 冻结位图/指纹测试脆弱且不值得维护；real-font 测试同样断言着墨区域与 AA 存在性，而不是依赖库版本、会变化的覆盖率字节 | `src/render/font.cpp`, [handoff.md](handoff.md) |
| HTML/CSS 支持是显式子集，已知限制有文档 | 这是浏览器玩具，不是规范符合性项目 | [roadmap.md](roadmap.md) |
| M10：文本默认使用自动系统字体选择。计算出的 CSS `font-family` 列表、`font-weight`、`font-style` 通过已批准的 fontconfig 库匹配（初始字体族 `serif`），文本按字符覆盖切成 run，缺失字符回退到覆盖它的其他已安装字体，根 `<html lang>` 作为 fontconfig 语言提示传入。`--font PATH` 强制单一 face，`--font-family NAME` 强制首选字体族（仍保留字符回退）；没有可用字体时退化到内置位图字体而不是让页面加载失败 | 用户要求不手动传参、像普通浏览器一样选字体（先支持英文与中文）；把位图路径保留为退化路径既让 headless 环境可用，也让测试能逐字节固定所有 M10 之前输出 | [roadmap.md](roadmap.md), [handoff.md](handoff.md), `src/shell/include/toy/shell/fonts.h` |
| 只支持 HTTP/1.1；TLS 使用已批准的 OpenSSL（自 M9 起链接）；HTTP 代理计划为 M16 但已被用户降低优先级；JavaScript 子集已暂缓 | 用户 2026-10-31 的优先级：需要 HTTPS 来测试真实站点，但从零实现 TLS 不合比例，因此使用已批准库；代理是计划中的外部样式表/资源加载/CSS 兼容弧之后的独立小轮次；JS 暂缓 | [roadmap.md](roadmap.md), [dependencies.md](dependencies.md), [open-questions.md](open-questions.md) |
| 假定输入为 UTF-8（剥离 BOM）；无编码检测 | 让 M2–M4 保持小；编码以后再加 | [roadmap.md](roadmap.md) |
| 每轮一个合并提交；仓库文件用英语，`README.zh.md` / `docs/zh/**` 除外 | 历史可读、仓库统一 | [AGENTS.md](../../AGENTS.md), [conventions.md](conventions.md) |

| Real-font 文本使用抗锯齿：freetype 8-bit 字形覆盖率用冻结的 SrcOver 公式混合；M10 把文本放在布局提供的基线上（`round(box.y) - scroll_y + round(box.baseline_offset_px)`，即行 ascent），使同一行不同字体共享基线；位图路径保持 cell 顶部、不透明、无抗锯齿的像素 | 让真实文本观感正常，并让混合字体的行正确；位图回退及其 golden 测试保持不变 | [architecture.md](architecture.md), `src/render/include/toy/render/paint.h`, `src/layout/include/toy/layout/box.h` |
| real-font 的有效字号被钳制到 `kMaxRealFontSizePx`（4096 px），用于度量、整形与光栅化 | 对抗性 CSS 字号不能让 freetype 分配无界的字形覆盖率位图；钳制让布局与绘制一致并让内存有界 | `src/render/include/toy/render/font.h`, [roadmap.md](roadmap.md) |
| M10 取代 M8b 的“每会话一个 face”规则：每会话一个 `Fonts` 选择把每个计算出的文本样式（字体族列表/weight/style + 页面语言）解析为 face 与 run；`--font PATH` 强制整个会话单一 face（无回退），`--font-family NAME` 强制首选字体族并保留字符回退；使用匹配文件的 face 0，未知字体族回退而不是报错 | M8b 当时只有整页选择，因此需要每会话一个 face；M10 的自动选择才是用户要求的普通浏览器行为，显式标志保持为简单确定的覆盖手段 | [architecture.md](architecture.md), `src/shell/include/toy/shell/fonts.h` |
| M9（已完成）是最小 HTTP/1.1-over-TLS 切片：用 OpenSSL 抓取绝对 `https://` URL，始终校验证书与主机名（使用系统/默认信任库，含 SNI），没有 insecure 模式，也不做 HTTP/2/ALPN、压缩、cookie/会话、HSTS、客户端证书、TLS 会话复用或 HTTP 缓存 | 用户要求“最基础的 HTTPS 请求”并强调 MVP；安全基本线的正确性不可妥协，其他都刻意排除 | [roadmap.md](roadmap.md), [dependencies.md](dependencies.md) |
| M9（已完成）保持 `FetchOptions` 不变并复用现有超时参数：TCP 连接用 `connect_timeout_ms`，TLS 握手与 TLS 读用 `read_timeout_ms` | MVP 不增加新配置面；现有阻塞 `Fetch()` 契约与错误风格保持不变 | [roadmap.md](roadmap.md) |
| M9（已完成）用 `net::ResolveUrl` 解析重定向 `Location`，允许 http<->https 目标并拒绝 file 目标 | 复用已测试的 RFC 3986 解析器而不是旧的手写重定向解析器，改动更小，同时修复相对/协议相加重定向 | [roadmap.md](roadmap.md), [handoff.md](handoff.md) |
| M9（已完成）TLS 测试使用提交的自签测试 CA 加 `localhost` 叶证书/私钥与进程内 TLS loopback 服务器；没有测试访问公网 | 确定性、headless、独立于机器连通性与公共 CA 轮换；仅测试用的证书在 fixture README 中文档化 | [roadmap.md](roadmap.md), [handoff.md](handoff.md) |
| M9（已完成）用进程级、首次 https 抓取时惰性创建的 `SSL_CTX` 实现 TLS，与纯 HTTP 共享私有字节流抽象（`Stream`/`SocketStream`/`TlsStream`），失败保持普通可读的 `Fetch` 错误（公开头文件中无 OpenSSL 类型），TLS 路径每个进程忽略一次 `SIGPIPE` | 保持公开 `Fetch` 契约与错误风格不变，HTTP/1.1 代码只留一份，TLS 失败可读且不泄漏 OpenSSL 细节；SIGPIPE 保护是防止坏掉的对端杀死进程的最小机制 | [handoff.md](handoff.md), `src/net/stream_internal.h`, `src/net/tls.cpp` |
| M10（已完成）把 fontconfig 字体发现保持在 `shell` 内：公开的 `toy/shell/fonts.h` 中 `Fonts` pimpl 对象把计算样式 + 页面语言解析为候选 face，按（字体族列表、weight、slant、language）缓存候选列表、按文件缓存已加载 face，只暴露 `MeasureText` 与 `Runs`（paint runs）；layout 仍通过回调取度量、不依赖 render/fontconfig，paint 不负责字体发现 | 让已批准的 fontconfig 依赖留在 shell 层，使度量与绘制共享同一字体来源，并让 layout/paint 在没有已安装字体时仍可测试 | [architecture.md](architecture.md), `src/shell/include/toy/shell/fonts.h` |
| M10（已完成）按计算出的文本样式度量文本：`MeasureText` 接收 `(text, ComputedStyle)`，`TextMetrics` 携带 `ascent_px`，同一行每个文本盒得到该行最大 ascent 作为 `baseline_offset_px` | layout 保持不依赖字体，同时让绘制与度量能对每个样式选中同一字体，并让混合字体/字号共享行基线 | `src/layout/include/toy/layout/box.h`, [architecture.md](architecture.md) |
| M10（已完成）通过 `FontRunsProvider` 绘制真实字体文本：有序、非空且精确拼接成文本盒内容的 run，按每个 run 的整形宽度推进 pen；空 provider（或某文本盒返回空 run）必须逐字节复现 M10 之前的位图命令列表 | 显示列表保持纯数据，允许一个文本盒内混合脚本 run，并保留冻结的位图 golden 输出与 M9 字节兼容门禁 | `src/paint/include/toy/paint/paint.h`, [handoff.md](handoff.md) |
| M10（已完成）加入可读中文的语言相关断行：文本切成断行单元（空白分隔符；非 CJK 文本按最长词；CJK 字符各自成单元；简化 UAX#14 禁则使收尾标点不出现在行首、起始标点不出现在行尾），每个单元是普通的可度量文本盒。单元之间不插入空格，拉丁词永不拆开 | MVP 需要无空格的中文段落能换行、可阅读；复用 word 级盒模型让 M5 的断行、行高与基线机制不变，并让所有纯 ASCII golden 逐字节不变 | [architecture.md](architecture.md), `src/layout/text_break.cpp`, [roadmap.md](roadmap.md) |
| M10（已完成）用提交的自造 fixture 字体（常规、粗体、斜体、小汉字 face）与生成的测试 fontconfig 配置保持字体/配置测试确定性：fixture 配置（只有 fixture 字体，通用族映射到拉丁 face）用于自动选择与回退测试，空配置强制位图路径。测试从不使用已安装字体或机器字体配置 | 这套双配置既证明新的系统字体路径（确定性几何），也证明完整的 M10 之前位图兼容性，且无需在生产代码里加入“信任此字体”的后门 | [handoff.md](handoff.md), `tests/fixtures/fonts/README.md`, `tests/shell/CMakeLists.txt` |
| M11（已完成）在未指定 `CMAKE_BUILD_TYPE` 时默认优化构建（RelWithDebInfo），并对第一方目标加 `-ffp-contract=off` | 浏览器是 CPU 密集的，未优化的默认构建让普通页面慢数倍；禁止 FP contraction 让布局几何在不同构建类型与 GCC/Clang 间逐字节一致（由全量测试与字节兼容门禁验证） | `CMakeLists.txt`, [conventions.md](conventions.md) |
| M11（已完成）在 `render::Font` 内部缓存真实字体文本（私有、有界 LRU）：每个 Font 复用一个 hb_font/hb_buffer，shaped-run 缓存（16 MiB / 16k entries，键为精确 26.6 字号 + 文本），glyph 覆盖率缓存（32 MiB / 16k entries，键为 glyph id + 26.6 字号）。分配失败视为 miss；逐出不改变输出；与文档化的 Font 契约一样单线程 | 实测热点是每次 shaping 都新建 hb_face（使 harfbuzz shape-plan 缓存失效）、每帧用 freetype 重新光栅化每个字形；缓存让重复帧只做字形混合，同时保持所有像素与度量不变 | `src/render/font_ttf.cpp`, `src/render/font_cache_internal.h`, [architecture.md](architecture.md) |
| M11（已完成）在 shell 的 `Fonts` 内部缓存字体解析与文本度量（私有、有界、单线程）：无分配的解析样式快速路径与稳定 style id、文本度量 LRU（100k entries / 32 MiB，style+language+text+size）与 run LRU（100k / 16 MiB，返回 font+offset+length）；位图退化路径刻意绕过缓存 | 布局/重排与每个绘制帧都会对每个词调用 `MeasureText`/`Runs`，重复构造 StyleKey 并重跑逐码点回退；缓存让重排与 paint provider 步骤变便宜，且不改变任何度量 | `src/shell/fonts.cpp`, `src/shell/include/toy/shell/fonts.h`, [handoff.md](handoff.md) |
| M11（已完成）把显示列表裁剪到视口：背景 FillRect 与 DrawImage 只有在滚动平移后的精确设备像素矩形与视口相交时才发出；文本盒只有在其矩形四周扩大 `ceil(2 * font_size_px)` 后与视口相交时才发出（覆盖 glyph overhang/斜体/AA/取整）。裁剪发生在样式/图片/字体 run 查找之前，保留一个 viewport clip 对与命令顺序，渲染像素与不裁剪逐字节相同 | 滚动/resize 帧过去会重建并光栅化整页，而其实只有约 0.4% 的文本盒可见；这是纯优化 + 保守余量，冻结的显示列表契约现在明确允许省略可证明不可见的命令 | `src/paint/paint.cpp`, `src/paint/include/toy/paint/paint.h`, [architecture.md](architecture.md) |
| M11（已完成）修正窗口循环：Expose 只重绘不重排（仅当窗口尺寸与已布局视口不同才 SetViewport），resize 只有尺寸真正变化才重排，X11 后端合并连续的 Expose/ConfigureNotify 队列事件（输入/close 事件永不合并、丢弃或乱序；尺寸未变的 configure burst 不产生事件），每个窗口尺寸复用一个 `render::Surface` | 打开窗口会布局两次，resize 拖拽的每个 ConfigureNotify 都会重排并重绘；这些改动去掉冗余工作并防止事件队列堆积，且不改变任何渲染输出 | `src/platform/window.cpp`, `src/platform/include/toy/platform/window.h`, `src/shell/application.cpp`, [handoff.md](handoff.md) |
| M11（已完成）刻意保留软件渲染，不添加 GPU 后端；窗口仍用 XPutImage 呈现整个 surface（XShm/dirty-rect 是后续工作，需要新依赖审批） | 实测表明填充率不是瓶颈（系统字体光栅化在 800×600 与 1600×1000 下耗时相同，瓶颈是 shaping/字形处理而不是像素），而 GPU 后端会增加平台/依赖/headless 回退复杂度且无法解决实测的 CPU 问题。只有在本轮缓存/裁剪后目标尺寸/DPI 下光栅化仍超帧预算时才重新评估 | [roadmap.md](roadmap.md), [handoff.md](handoff.md) |
| fixture 生成器与它们生成的 fixture 一起提交（`tests/fixtures/fonts/generate_toy_test.py`、`tests/fixtures/fonts/generate_variants.py`、`tests/fixtures/images/generate.py`），只用 Python 标准库、相对脚本所在目录写文件且输出确定；fixture README 记录重新生成方法与测试依赖的属性 | fixture 是测试依赖的已提交二进制产物；把生成器纳入版本控制使其可复现、可审计，而不是依赖一次性脚本。重新生成必须复现已提交内容（字体生成器逐字节相同；PNG 字节可能随本地 zlib 变化），改动 fixture 仍须在同一改动中更新 README 与测试 | `tests/fixtures/fonts/README.md`, `tests/fixtures/images/README.md`, [conventions.md](conventions.md) |
| M12–M15 的顺序是外部样式表（M12）→ 资源加载管线（M13）→ CSS 兼容批次 1（M14）→ flexbox/position/float 子集（M15）；HTTP 代理移到 M16，JavaScript 保持延后 | 2026-10-31 的真实站点实测表明：页面没有样式是因为外部 CSS 被忽略；图片较多的页面大部分墙体时间花在逐资源的新 TLS 连接上，而 CPU 很低。外部 CSS 是真实页面样式的最小功能前提，且不需要新依赖；资源管线是一个有限、可度量的收益，也会加速后续 CSS 工作；布局/CSS 兼容弧更大，拆成多轮以保持每次可审查 | [roadmap.md](roadmap.md), [handoff.md](handoff.md) |

如果某个决策变得不合适，不要悄悄偏离：在 [open-questions.md](open-questions.md) 中提出，或与用户讨论，然后在同一轮更新本文件、受影响的文档与代码。
