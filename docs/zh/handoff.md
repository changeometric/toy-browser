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
| 审阅轮 | 修复无上限的 `--size`、文件 body 上限、dump 缩进；20 个 CTest 测试 |

尚未实现：真实 TTF 字体（M8b）、JavaScript（M9）、`https`/TLS、代理、外部样式表、border、抗锯齿文本、水平滚动/滚动条、hover/active 链接样式与表单控件。PNG 图片自 M8a 起可用（M8a 已知限制见 [roadmap.md](roadmap.md)）。窗口与 `--screenshot` 在给出 URL 时绘制真实页面，否则显示 M1 占位页；给出页面 URL 时窗口也可浏览（链接、滚轮/键盘滚动、Alt+Left/Right 历史、`<title>`）。`--dump-*` 模式仍是主要调试路径。

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

预期：46/46 测试通过；使用 `-DTOYBROWSER_WERROR=ON` 构建在 GCC 和 Clang 上无警告；dump 输出确定性的树；截图写出二进制 PPM（带 URL 的形式绘制真实页面，不带 URL 的形式保留占位页）。所有 headless 模式都不需要显示器。需要 CMake 3.20 或更新版本；构建需要 X11 与 libpng 开发头文件（均为已批准的依赖）；只有窗口模式需要运行中的显示服务器。

```sh
"$BUILD"/toybrowser --size 320x200 --screenshot /tmp/toybrowser-img.ppm \
    "file://$PWD/tests/fixtures/img_site/index.html" && file /tmp/toybrowser-img.ppm
"$BUILD"/toybrowser --size 320x200 --dump-boxes \
    "file://$PWD/tests/fixtures/img_site/index.html" | grep '<img>'
```

预期：截图绘制提交的 PNG fixtures（旗子、alpha、灰度、调色板），盒 dump 包含带固有/CSS 尺寸的
`<img> inline` 盒；`missing.png` 不保留盒。

```sh
"$BUILD"/toybrowser --size 400x200 --screenshot /tmp/toybrowser-nav.ppm \
    "file://$PWD/tests/fixtures/nav_site/index.html" && file /tmp/toybrowser-nav.ppm
```

窗口演示（需要显示器）：

```sh
"$BUILD"/toybrowser --size 320x600 "file://$PWD/tests/fixtures/img_site/index.html"
"$BUILD"/toybrowser --size 400x200 "file://$PWD/tests/fixtures/nav_site/index.html"
# 图片页：确认 PNG 图片被绘制；导航页：点击 "About page"，用滚轮/方向键/PageUp/PageDown/Home/End
# 滚动，Alt+Left/Right 前进后退；窗口标题跟随 <title>。
```

## 下一轮：M8b —— 真实字体（库已批准）

- 目标：真实 TTF 文本：freetype 负责度量与字形光栅化，harfbuzz 负责整形，fontconfig 负责已安装
  字体的发现/匹配。用户于 2026-10-31 批准了这三个库，并澄清“从零”适用于浏览器功能，而不是
  底层编解码/字体机制（记录在 [dependencies.md](dependencies.md) 与 [decisions.md](decisions.md)）。
  缺失的开发包需要用户安装；agent 从不安装任何东西。
- 初步范围：`render` 增加字体后端（加载、度量、整形后的 advance、抗锯齿字形覆盖率），在 TTF
  路径上替换或扩展内置 8x16 位图字体；`layout` 继续通过既有 `MeasureText` 回调消费文本度量
  （基线信息可能加入契约）；`paint` 放置真实字形并发出覆盖率文本；字体选择/回退（默认或配置的
  字体）在本轮开始时冻结。未选择 TTF 字体的页面应保持位图字体输出不变。
- 测试必须保持确定性，且不得依赖已安装字体：由提交/生成的字体 fixture 驱动测试；已安装的真实
  字体只用于手工演示。
- 当一个带真实 TTF 文本和至少一张图片的页面渲染出来时完成（即最初的 M8 验收），且所有既有测试
  （46 个）、GCC/Clang 无警告构建与 M8a 图片行为仍然通过。
- 仍不在范围内：JavaScript（M9）、表单、`https`/TLS、代理、外部 CSS。

M8b 之后的 backlog（细节见 [roadmap.md](roadmap.md)）：代理支持、TLS、更多图片格式/背景图，以及
测试辅助代码迁移。

## 仓库地图

- `src/<module>/` —— 每个模块一个 CMake 库目标（`base` 尚不存在；模块图与依赖方向见 [architecture.md](architecture.md)）。
- `src/main.cpp` —— 只做 CLI 解析；`shell` 编排 fetch → parse → style → images → layout → paint →
  dump/present，`toy/shell/browser.h` 拥有一个浏览器会话（历史、滚动、点击、标题）。
- `src/shell/page.h` —— shell 私有的页面加载器（最终 URL、DOM、样式、盒树、解码后的图片）；
  `src/render/include/toy/render/image.h` —— `Image` 与基于 libpng 的 `DecodePng`；显示列表与光栅化器在
  `toy/render/paint.h`，`src/paint/` 从盒树构建列表且不光栅化。`src/render/include/toy/render/font.h` ——
  位图字体度量；`layout` 通过 `shell` 提供的回调消费它们，自身不依赖 `render`。
- `tests/<module>/` —— 每个主题一个可执行文件，注册到 CTest；新测试使用 header-only 的
  `tests/support/check.h`。提交的 fixtures：`tests/fixtures/nav_site/`（三页导航演示）、
  `tests/fixtures/img_site/`（图片流水线演示）、`tests/fixtures/images/`（像素内容有文档的手工 PNG）。
- `docs/` —— 架构、路线图、约定、团队流程、决策、依赖、待决问题（本文件是快照）。
- 共享任务板 —— 由 agent 运行时提供（任务 create/claim/update，带 revision）；如果运行时没有，则用 `docs/tasks/<milestone>.md`，字段相同。见 [team-workflow.md](team-workflow.md)。
- `README.md` / `README.zh.md` 与 `docs/zh/` —— 面向用户的入口（英语为权威）。
- `.tmp/` —— 被 gitignore 的本地暂存区，用于笔记、fixture、下载与测试输出。

## 冻结契约（不要随意改动）

- CLI 模式、标志与退出码：`0` 成功，`1` 运行时错误，`2` 用法错误。可选的位置参数页面 URL（`toybrowser [options] [url]`）只用于窗口模式和 `--screenshot`；最多一个 URL，不能与 `--dump-*` 模式组合，不给 URL 时保留 M1 占位页。
- Dump 格式（DOM、style、boxes）：确定性、面向行、由测试断言；任何改动都需要同一轮内更新测试和文档。盒 dump 是前序遍历的行：`<tag> block|inline`、`anonymous block` 或 `text "..."`，后接 `x= y= width= height=`，全部是绝对 CSS 像素取整后的整数（`std::lround`）；缩进为每层盒深度两个空格，上限 64 层。
- 布局语义通过布局测试与 M5/M8a 的 known-limits 冻结：视口宽度来自 `--size`，没有 margin 合并，行顶部对齐，文本盒以单词为单位，仅按 ASCII 空白换行。M8a replaced 元素：`LayoutOptions::replaced_sizes`（DOM 节点 id → 固有 CSS px）把行内元素变成原子行内级盒；CSS `width`/`height` px 覆盖固有尺寸，只指定一侧时保持固有宽高比，盒整体换行、抬高行高并顶部对齐；其 dump 行仍是 `<tag> inline x= y= width= height=`，没有子节点，`display:block` 的 replaced 元素不支持，replaced 盒的 margin/padding 被忽略。没有条目的页面保持原有几何逐字节不变。
- 绘制语义通过 M6/M7 测试冻结：显示列表命令使用整数设备像素（`FillRect`、`DrawText`、`DrawImage`、`PushClip`/`PopClip`）；填充对不透明目标使用 SrcOver 公式 `(src * a + dst * (255 - a) + 127) / 255`；初始裁剪是整个 surface，光栅化工作量受裁剪限制；`shell` 把 canvas 填白；绘制顺序是盒树前序（先背景、再图片、再子节点/文本）；文本使用内置 8x16 ASCII 字形，最近邻缩放、无抗锯齿、非 ASCII 用 `?`、文本像素不透明。
  M7 绘制：非空页面恰好被一个 `PushClip(0, 0, viewport)`/`PopClip` 对包裹，每条命令的 y 按
  `-scroll_y`（整数设备像素，x 不变）平移；没有图片的页面在滚动 0 必须与 M6/M7 逐字节一致。
  M8a 图片：节点 id 在 `paint::ImageMap` 中的行内 replaced 盒各发出一条 `DrawImage`，在该盒背景之后；
  命令持有 `shared_ptr<const render::Image>`；目标像素 (dx, dy) 用整数向下取整采样源
  `((dx - x) * image.width / width, (dy - y) * image.height / height)`（最近邻，无插值/抗锯齿），
  用同一 SrcOver 公式混合（alpha 0 无操作，alpha 255 写 `0xFF000000 | rgb`，结果不透明）；空图片映射
  精确复现 M8a 之前的命令列表。
- 图片解码契约（`render::DecodePng`）：只支持 PNG，经由接口后的已批准 libpng 依赖，输出直通 alpha 的
  ARGB `render::Image`；接受尺寸为每边最多 8192 px 且总计最多 16,777,216 像素（包含式上界），在分配前
  拒绝；失败返回 false 并给出可读错误、`*out` 不变；不抛异常；公开头文件中不出现 libpng 类型。
- Shell 页面加载器契约（`src/shell/page.h`，私有）：抓取/解析/样式之后，按文档顺序（迭代遍历）加载每个
  `<img>`，取第一个 `src`，相对页面最终 URL 解析，用 `net::Fetch`（仅 http/file）抓取并解码，维护按
  img DOM 节点 id 索引的 `layout::ReplacedSizes` + `paint::ImageMap`。缺失/失败/不支持的图片被静默跳过
  （没有 broken-image UI、没有 stderr、页面加载仍成功，元素按空行内布局）；没有图片缓存（每次出现各
  抓取一次），即使元素不可见也会抓取。`--dump-boxes` 使用同一加载器；`--dump-dom`/`--dump-style`
  不抓取图片。
- 导航/会话契约（`toy/shell/browser.h`）：每次成功加载一条历史，加载失败不改变任何东西，每次
  成功加载把滚动偏移重置为 0，只改变 fragment 的引用是 no-op，标题是第一个 `<title>` 文本
  （折叠空白，没有时回退到序列化最终 URL），只有 `http`/`file` 链接会导航。M8a 保持公开方法完全
  不变，但用前向声明、out-of-line 析构函数与 `std::unique_ptr` 隐藏页面（DOM/样式/盒树/图片），
  因此头文件不再暴露页面数据，`Browser` 不可拷贝。
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
- 还没有 `https`：大多数真实站点无法抓取；`http://` 和 `file://` 可用。
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
- 内置 8x16 字形表是手写的，字形形状靠肉眼复核（用户偏好）：刻意不保留冻结的字形位图/指纹测试。
  改字形就是视觉改动——提交前渲染一个样例看一眼。
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
