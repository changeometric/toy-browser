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
| 审阅轮 | 修复无上限的 `--size`、文件 body 上限、dump 缩进；20 个 CTest 测试 |

尚未实现：导航、滚动与交互、图像、真实 TTF 字体、JavaScript、`https`/TLS、代理、外部样式表、border、抗锯齿文本。窗口与 `--screenshot` 在给出 URL 时绘制真实页面，否则显示 M1 占位页；`--dump-*` 模式仍是主要调试路径。

## 验证当前状态（无需上下文）

以下命令请在仓库根目录执行：

```sh
BUILD=build/onboard-check
cmake -S . -B "$BUILD" -DTOYBROWSER_WERROR=ON
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

预期：37/37 测试通过；使用 `-DTOYBROWSER_WERROR=ON` 构建在 GCC 和 Clang 上无警告；dump 输出确定性的树；截图写出二进制 PPM（带 URL 的形式绘制真实页面，不带 URL 的形式保留占位页）。所有 headless 模式都不需要显示器。需要 CMake 3.20 或更新版本；构建需要 X11 开发头文件（已批准的依赖）；只有窗口模式需要运行中的显示服务器。

## 下一轮：M7 —— 导航（尚未开始）

- 目标：让绘制出来的页面可交互、可导航。用盒树对点击做命中测试以跟随链接，加入滚动（视口偏移加显示列表中的裁剪命令）、前进/后退历史，并根据 `<title>` 设置窗口标题。
- 初步范围：`shell` 拥有导航状态（当前 URL、历史、滚动偏移），窗口事件循环转发点击/滚动/按键；`paint`/`render` 补齐架构中已经勾画的裁剪与偏移通路；`net` 提供被点击页面的 URL（`href` 相对当前 URL 解析；base URL 解析与外部 `<link>` CSS 仍留到以后）。`--dump-*` 模式必须保持 headless 且不变。
- 仍不在范围内：图像、真实 TTF 字体（M8）、表单、JavaScript（M9）、`https`/TLS。
- 验收草案：一个小的本地多页站点可以在窗口中浏览（点击链接、前进/后退、滚动、标题），至少有一个针对 URL/命中测试/滚动状态的 headless 回归测试；所有现有测试保持通过；两个编译器下无警告。
- 导航相关决策（历史粒度、滚动单位、点击如何映射到盒）是 M7 intake 的工作——不要在代码里猜。

M7 之后的 backlog（细节见 [roadmap.md](roadmap.md)）：M8 的图像/字体库决策、代理支持、TLS，以及测试辅助代码迁移。

## 仓库地图

- `src/<module>/` —— 每个模块一个 CMake 库目标（`base` 尚不存在；模块图与依赖方向见 [architecture.md](architecture.md)）。
- `src/main.cpp` —— 只做 CLI 解析；`shell` 编排 fetch → parse → style → layout → paint → dump/present。
- `src/render/include/toy/render/font.h` —— 位图字体度量；`layout` 通过 `shell` 提供的回调消费它们，自身不依赖 `render`。`src/render/include/toy/render/paint.h` 是显示列表与光栅化器；`src/paint/` 从盒树构建列表且从不光栅化。
- `tests/<module>/` —— 每个主题一个可执行文件，注册到 CTest；新测试使用 header-only 的 `tests/support/check.h`。
- `docs/` —— 架构、路线图、约定、团队流程、决策、依赖、待决问题（本文件是快照）。
- 共享任务板 —— 由 agent 运行时提供（任务 create/claim/update，带 revision）；如果运行时没有，则用 `docs/tasks/<milestone>.md`，字段相同。见 [team-workflow.md](team-workflow.md)。
- `README.md` / `README.zh.md` 与 `docs/zh/` —— 面向用户的入口（英语为权威）。
- `.tmp/` —— 被 gitignore 的本地暂存区，用于笔记、fixture、下载与测试输出。

## 冻结契约（不要随意改动）

- CLI 模式、标志与退出码：`0` 成功，`1` 运行时错误，`2` 用法错误。可选的位置参数页面 URL（`toybrowser [options] [url]`）只用于窗口模式和 `--screenshot`；最多一个 URL，不能与 `--dump-*` 模式组合，不给 URL 时保留 M1 占位页。
- Dump 格式（DOM、style、boxes）：确定性、面向行、由测试断言；任何改动都需要同一轮内更新测试和文档。盒 dump 是前序遍历的行：`<tag> block|inline`、`anonymous block` 或 `text "..."`，后接 `x= y= width= height=`，全部是绝对 CSS 像素取整后的整数（`std::lround`）；缩进为每层盒深度两个空格，上限 64 层。
- 布局语义通过布局测试与 M5 的 known-limits 冻结：视口宽度来自 `--size`，没有 margin 合并，行顶部对齐，文本盒以单词为单位，仅按 ASCII 空白换行。
- 绘制语义通过 M6 测试冻结：显示列表命令使用整数设备像素（`FillRect`、`DrawText`、`PushClip`/`PopClip`）；填充对不透明目标使用 SrcOver 公式 `(src * a + dst * (255 - a) + 127) / 255`；初始裁剪是整个 surface，光栅化工作量受裁剪限制；`shell` 把 canvas 填白；绘制顺序是盒树前序（先背景、再子节点）；文本使用内置 8x16 ASCII 字形，最近邻缩放、无抗锯齿、非 ASCII 用 `?`、文本像素不透明。
- [architecture.md](architecture.md) 中的模块边界与依赖方向。
- `--size` 每边被钳制在 1..8192，且总像素最多 16M；body 由 `FetchOptions::max_body_bytes` 限制（默认 16 MiB）；dump 缩进最多 64 层。
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
- 还没有 `https`：大多数真实站点无法抓取；`http://` 和 `file://` 可用。
- HTML/CSS/URL 解析器按设计是容错的；它们遇到畸形输入永不失败，所以“没有报错”不是正确性的证据——把 dump 与 golden 测试比较。
- `--dump-boxes` 用 `--size` 的宽度作为视口并忽略高度；高度由内容决定。`layout` 绝不能包含 `render` 头文件——文本度量只通过回调到达。
- 绘制刻意使用整数像素、不做抗锯齿：改变取整、字形缩放或绘制顺序都会改变 golden 像素测试。GCC 与 Clang 必须产出逐字节相同的 PPM——用 `cmp` 比较两套构建的截图是很便宜的确定性门。
- 绘制顺序在文档中是前序的“先背景、再子节点”；还没有层叠上下文，所以未经契约改动不要加入 alpha/重叠语义。
- canvas 由 `shell` 填白；`html`/`body` 的 background-color 只绘制其元素盒（不会传播为 canvas 背景）——这是刻意的 M6 限制，不是 bug。
- 光栅化器的工作量必须始终受裁剪/surface 区域限制：对抗性字号曾让旧的字形循环挂起或溢出；修改 `render::DrawDisplayList` 时保留有限性守卫、可见范围迭代与有界单元数学。
- 临时文件（fixture、截图、笔记、下载）放在仓库根目录被 gitignore 的 `.tmp/`，不要散落在树中。
