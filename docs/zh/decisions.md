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
| 绘制使用整数设备像素；默认文本路径是内置 8x16 位图字体（最近邻缩放、无抗锯齿），可选的 real-font 文本（M8b）使用 freetype 8-bit 覆盖率抗锯齿 | 确定性的像素测试与逐字节相同的 GCC/Clang 截图；位图默认路径让 M8b 之前的输出逐字节不变 | [architecture.md](architecture.md), [roadmap.md](roadmap.md) |
| `shell` 把页面 canvas 填白；元素 `background-color` 只绘制该元素的盒（不做 canvas 背景传播） | 让 MVP 绘制器显式而简单；真实站点观感不对时再重新考虑 | [architecture.md](architecture.md), [roadmap.md](roadmap.md) |
| 导航状态（当前 URL、历史、滚动偏移）放在 headless 的 `shell::Browser` 中；窗口事件循环与 `--screenshot` 只是其上的薄呈现层 | 让点击、历史、滚动与标题不开窗口也能测试 | [architecture.md](architecture.md), `src/shell/include/toy/shell/browser.h` |
| 点击激活：用滚动后的页面坐标对盒树做命中测试，再沿 DOM 祖先找最近的带 `href` 元素；只有 `http`/`file` 引用会导航，纯 fragment 引用是 no-op，加载失败保留当前页面、滚动偏移与历史 | 确定性的链接语义，且 `layout`/`render` 不需要了解 HTML | [architecture.md](architecture.md) |
| 滚动只有垂直方向、以整数设备像素表示，范围钳制在 `[0, content_height - viewport_height]`；`paint` 用视口 `PushClip`/`PopClip` 对包裹页面并把每条命令按 `-scroll_y` 平移；导航把偏移重置为 0 | 最小确定性滚动；恢复滚动位置是有文档的 MVP 限制 | [architecture.md](architecture.md), [roadmap.md](roadmap.md) |
| 平台输入归一化（左键点击、滚轮格数、带修饰键的映射按键、`SetTitle`）；`shell` 拥有按键绑定与滚动单位 | 把 X11 细节挡在 `shell` 之外，把页面内容挡在 `platform` 之外 | [architecture.md](architecture.md) |
| M8 使用已批准的底层库而不是自写编解码：libpng + zlib 解 PNG（M8a），freetype 负责字形、harfbuzz 负责整形、fontconfig 负责字体发现（M8b） | 用户于 2026-10-31 决定：“从零”指浏览器功能（网络、HTML/CSS、布局、绘制、JS），不包括底层编解码或字体机制；已批准的库都是宽松许可证，并让 M8 可控 | [dependencies.md](dependencies.md), [roadmap.md](roadmap.md) |
| M8 拆成 M8a（PNG 图片）与 M8b（真实字体）两轮 | 两个更小、可独立审查的轮次；图片与字体相互独立且风险更低 | [roadmap.md](roadmap.md) |
| 图片以整数设备像素的显示列表 `DrawImage` 命令绘制，使用冻结的最近邻源映射与和填充相同的 SrcOver 公式；没有解码图片的页面产出与 M8a 之前完全相同的命令列表 | MVP 中缩放与混合确定、逐字节兼容，不做插值或抗锯齿 | [architecture.md](architecture.md), `src/render/include/toy/render/paint.h` |
| replaced 元素的固有尺寸以调用方提供的、按 DOM 节点 id 索引的映射传给布局；布局从不依赖 render 或图片 | 保持 layout → render 边界，并让布局可用假尺寸测试 | [architecture.md](architecture.md), `src/layout/include/toy/layout/box.h` |
| 手写位图字形表靠肉眼复核；不保留冻结的字形位图/指纹测试 | 用户更愿意自己看字形，而不是维护硬编码期望；real-font 测试同样断言着墨区域/AA 存在性，而不是依赖库版本、会变化的覆盖率字节 | `src/render/font.cpp`, [handoff.md](handoff.md) |
| HTML/CSS 支持是显式子集，已知限制有文档 | 这是浏览器玩具，不是规范符合性项目 | [roadmap.md](roadmap.md) |
| 文本默认使用内置位图字体；真实 TTF 字体是按运行可选（M8b），通过 `--font PATH` 或 fontconfig 的 `--font-family NAME` 选择（freetype + harfbuzz + fontconfig） | 在布局存在前避免字体依赖；opt-in 让 M8b 之前的 dump 与截图逐字节不变，并让测试不依赖已安装字体 | [roadmap.md](roadmap.md), [handoff.md](handoff.md) |
| 只支持 HTTP/1.1；尚无 TLS 与代理（用户已推迟） | 与玩具范围相称；需要时再考虑 | [roadmap.md](roadmap.md), [open-questions.md](open-questions.md) |
| 假定输入为 UTF-8（剥离 BOM）；无编码检测 | 让 M2–M4 保持小；编码以后再加 | [roadmap.md](roadmap.md) |
| 每轮一个合并提交；仓库文件用英语，`README.zh.md` / `docs/zh/**` 除外 | 历史可读、仓库统一 | [AGENTS.md](../../AGENTS.md), [conventions.md](conventions.md) |

| Real-font 文本使用抗锯齿：freetype 8-bit 字形覆盖率用冻结的 SrcOver 公式混合，显示列表命令把文本放在基线上（`round(box.y) - scroll_y + round(font ascent)`）；位图路径保持 cell 顶部、不透明、无抗锯齿的像素 | 让真实文本观感正常，同时保持默认位图渲染器及其 golden 测试不变 | [architecture.md](architecture.md), `src/render/include/toy/render/paint.h` |
| real-font 的有效字号被钳制到 `kMaxRealFontSizePx`（4096 px），用于度量、整形与光栅化 | 对抗性 CSS 字号不能让 freetype 分配无界的字形覆盖率位图；钳制让布局与绘制一致并让内存有界 | `src/render/include/toy/render/font.h`, [roadmap.md](roadmap.md) |
| 字体选择由 shell 每次运行解析一次：字体文件（`--font`）或 fontconfig 对字体族的最佳匹配（`--font-family`），只用 face 0，整个会话一个 face；未知字体族回退而不是报错 | 最小且确定性的字体选择契约；fontconfig 的回退行为符合其正常语义，每会话一个 face 保持 MVP 范围清晰 | [architecture.md](architecture.md), `src/shell/include/toy/shell/application.h` |

如果某个决策变得不合适，不要悄悄偏离：在 [open-questions.md](open-questions.md) 中提出，或与用户讨论，然后在同一轮更新本文件、受影响的文档与代码。
