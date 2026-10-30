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
| 绘制使用整数设备像素、不做抗锯齿；文本使用内置 8x16 位图字形与最近邻缩放 | 无需字体依赖即可获得确定性的像素测试和逐字节相同的 GCC/Clang 截图 | [architecture.md](architecture.md), [roadmap.md](roadmap.md) |
| `shell` 把页面 canvas 填白；元素 `background-color` 只绘制该元素的盒（不做 canvas 背景传播） | 让 MVP 绘制器显式而简单；真实站点观感不对时再重新考虑 | [architecture.md](architecture.md), [roadmap.md](roadmap.md) |
| 导航状态（当前 URL、历史、滚动偏移）放在 headless 的 `shell::Browser` 中；窗口事件循环与 `--screenshot` 只是其上的薄呈现层 | 让点击、历史、滚动与标题不开窗口也能测试 | [architecture.md](architecture.md), `src/shell/include/toy/shell/browser.h` |
| 点击激活：用滚动后的页面坐标对盒树做命中测试，再沿 DOM 祖先找最近的带 `href` 元素；只有 `http`/`file` 引用会导航，纯 fragment 引用是 no-op，加载失败保留当前页面、滚动偏移与历史 | 确定性的链接语义，且 `layout`/`render` 不需要了解 HTML | [architecture.md](architecture.md) |
| 滚动只有垂直方向、以整数设备像素表示，范围钳制在 `[0, content_height - viewport_height]`；`paint` 用视口 `PushClip`/`PopClip` 对包裹页面并把每条命令按 `-scroll_y` 平移；导航把偏移重置为 0 | 最小确定性滚动；恢复滚动位置是有文档的 MVP 限制 | [architecture.md](architecture.md), [roadmap.md](roadmap.md) |
| 平台输入归一化（左键点击、滚轮格数、带修饰键的映射按键、`SetTitle`）；`shell` 拥有按键绑定与滚动单位 | 把 X11 细节挡在 `shell` 之外，把页面内容挡在 `platform` 之外 | [architecture.md](architecture.md) |
| 手写位图字形表靠肉眼复核；不保留冻结的字形位图/指纹测试 | 用户更愿意自己看字形，而不是维护硬编码期望；M8 的真实字体会替换或扩展该表 | `src/render/font.cpp`, [handoff.md](handoff.md) |
| HTML/CSS 支持是显式子集，已知限制有文档 | 这是浏览器玩具，不是规范符合性项目 | [roadmap.md](roadmap.md) |
| 文本从内置位图字体开始；真实 TTF 推迟到 M8 | 在布局存在前避免字体依赖 | [roadmap.md](roadmap.md) |
| 只支持 HTTP/1.1；尚无 TLS 与代理（用户已推迟） | 与玩具范围相称；需要时再考虑 | [roadmap.md](roadmap.md), [open-questions.md](open-questions.md) |
| 假定输入为 UTF-8（剥离 BOM）；无编码检测 | 让 M2–M4 保持小；编码以后再加 | [roadmap.md](roadmap.md) |
| 每轮一个合并提交；仓库文件用英语，`README.zh.md` / `docs/zh/**` 除外 | 历史可读、仓库统一 | [AGENTS.md](../../AGENTS.md), [conventions.md](conventions.md) |

如果某个决策变得不合适，不要悄悄偏离：在 [open-questions.md](open-questions.md) 中提出，或与用户讨论，然后在同一轮更新本文件、受影响的文档与代码。
