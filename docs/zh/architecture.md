> 本文为中文翻译，英文原版为准：[architecture.md](../architecture.md)。

# 架构

面向 MVP 的设计：让每个里程碑跑通的最薄实现。这里没有任何推测性内容——抽象只在真实切片需要时才加入。

## 模块划分

| 模块 | 职责 | 可依赖 | 首次需要 |
| --- | --- | --- | --- |
| `base` | 小型共享工具（几何、日志、UTF-8），只在出现需要时才存在 | — | 首次需要时 |
| `platform` | 窗口、输入事件、事件循环、呈现像素表面 | `base` | M1 |
| `render` | 显示列表、软件光栅化器、内置位图字体（度量与字形）、PNG 图片解码（已批准的 libpng）、真实字体引擎（已批准的 freetype + harfbuzz） | `base` | M1 |
| `net` | URL 解析、socket、HTTP/1.1 抓取 | `base` | M2 |
| `dom` | HTML tokenizer（词法分析器）、树构建器、DOM arena（区域分配器） | `base` | M3 |
| `css` | CSS tokenizer、规则/声明解析器、选择器 | `base` | M4 |
| `style` | 层叠与计算样式、默认样式表 | `dom`、`css` | M4 |
| `layout` | 盒树、块级/行内布局、换行 | `dom`、`style`、来自 `shell` 的文本度量回调 | M5 |
| `paint` | 从盒树构建显示列表（元素背景与文本） | `dom`、`style`、`layout`、`render` | M6 |
| `js` | 小型 JavaScript 引擎与 DOM 绑定（可选，最后） | `dom`、`base` | M9 |
| `shell` | 导航、流水线编排、窗口外壳、字体选择（字体族查找使用已批准的 fontconfig） | 所有模块 | M1 |

依赖只向下指向；没有环。`render` 完全不了解 HTML/CSS（它消费显示列表）；`paint` 认识 DOM/样式/布局数据但从不光栅化；`platform` 不了解页面内容（它 blit 像素并上报输入）。只有 `shell` 能看到所有模块并把它们接在一起。`base` 尚不存在：M1–M6 的模块只用标准库与标准 POSIX 接口，只有当共享工具确实需要时该模块才出现。

新模块注册在根与 `tests` `CMakeLists.txt` 的显式列表中；Lead 添加一次名字。`--dump-*` 模式与退出码（0 成功，1 运行时错误，2 用法错误）是冻结接口。

## 一次页面加载的完整流程

1. `shell` 收到 URL（CLI 参数或链接点击）。
2. `net` 抓取它：字节、最终 URL、状态。
3. 字节被解码成 UTF-8（MVP：假定 UTF-8，剥掉 BOM；其他编码以后再说）。
4. `dom` 把 HTML 解析成 `Document`。
5. `css` 解析样式表（先支持 `<style>` 和 `style=` 属性；外部 CSS 以后再加）。
6. `style` 把层叠解析成样式树。
7. `shell` 遍历 DOM 中的 `<img>` 元素，对每个首个 `src` 相对页面最终 URL 解析、抓取，并用
   `render::DecodePng` 解码；失败被跳过（M8a）。
8. `layout` 把样式树变成带几何信息的盒树，replaced 元素使用 shell 提供的固有尺寸。
9. `paint` 从盒树构建扁平显示列表（背景、图片、文本），`render` 把它光栅化成 `Surface`。
10. `platform` 呈现该表面（窗口 blit，或在 headless 模式下输出 PPM dump）。

MVP 保持整条链路同步、单线程。每个阶段接收普通数据并返回普通数据，因此每个阶段都能在测试中通过 `--dump-*` 命令单独驱动。

网络（M2）：`net` 拥有 URL、socket 和 HTTP。抓取是阻塞式 HTTP/1.1 GET——每个连接一个请求、连接与读取超时、`Content-Length` 或 chunked body、重定向、无 TLS、无 keep-alive——封装在 `Fetch()` 之后，因此以后可以改成非阻塞而不改调用方。`net` 是唯一允许触碰 socket 的模块；页面代码永远看不到 socket。代理支持（先支持 HTTP 代理）计划放在 `FetchOptions` 后面并保持在 `net` 内部；它不需要新依赖。

HTML 与 DOM（M3）：`dom` 拥有 tokenizer、简化树构建器和 arena DOM。解析是容错的，且总是产生一个带 `head` 和 `body` 子节点的 `html` 元素；tokenizer 覆盖支持的子集（元素与属性、注释、doctype、raw-text 与 RCDATA 元素、少量实体），遇到畸形输入也永不失败。`DumpDocument()` 是确定性的文本形式，供 `--dump-dom` 与解析器测试使用。节点用整数 id 寻址（没有持有指针、没有环）；DOM 完全不了解 CSS、布局或脚本。

样式（M4）：`css` 把样式表与声明列表解析成普通结构，完全不了解 DOM。`style` 在 DOM 上匹配这些选择器，应用 UA/作者/行内层叠——依据 specificity（优先级）、`!important` 与继承——并为每个元素生成一个 `ComputedStyle`；内置的 UA 样式表本身就是由我们的解析器解析的 CSS。`DumpStyleTree()` 是 `--dump-style` 与样式测试背后的确定性文本形式。布局只会消费样式树，永远不会消费原始 CSS。

布局（M5）：`layout` 把样式树转换成带绝对 CSS 像素几何的 `BoxTree`。换行需要文本度量，因此 `LayoutDocument()` 接受一个由 `shell` 提供的 `MeasureText` 回调（只用标准类型）；内置等宽位图字体度量位于 `render`，由 `shell` 适配——若会话选择了真实字体，则改为适配该字体的整形 advance 与行高（M8b），布局本身仍然完全不了解字体。这样保持依赖方向不变：没有 `layout` → `render` 的边，`layout` 中也不重复字体代码。`DumpBoxTree()` 是 `--dump-boxes` 与布局测试背后的确定性文本形式。布局只消费样式树：它从不读取原始 CSS，也从不绘制。几何是输入的纯函数；MVP 简化的摘要见 [roadmap.md](roadmap.md) 的 known-limits 列表。M8a 把 replaced 元素加入同一套布局：`LayoutOptions::replaced_sizes` 把 DOM 节点 id 映射到 shell 提供的固有 CSS 像素尺寸；有条目的行内元素成为原子行内级盒（CSS width/height px 覆盖、只给一侧时保持固有宽高比、整体换行、顶部对齐、抬高行高），而 `layout` 仍然从不读取图片、也不依赖 `render`。

绘制（M6）：`paint` 按前序遍历盒树，产出扁平的 `render::DisplayList`：每个计算背景非透明的元素盒变成一条 `FillRect`（背景先于子内容），每个节点 id 有解码图片的行内 replaced 盒变成一条 `DrawImage`（M8a，在背景之后、子内容之前），每个文本盒变成一条 `DrawText`，使用继承的颜色与字号。`paint` 从不光栅化，也从不包含 render 的实现头文件。`render` 执行该列表：矩形填充对不透明目标使用冻结的 SrcOver 整数公式；文本使用内置 8x16 ASCII 字形表，按最近邻单元缩放（无抗锯齿）；图片使用冻结的最近邻源映射（见下）；整数矩形裁剪栈约束每条命令（M7 会用视口 `PushClip`/`PopClip` 对包裹页面，并把每条命令按滚动偏移平移）。`shell` 把 canvas 填白、按 surface 宽度布局页面、构建列表并光栅化；窗口与 `--screenshot` 共享完全相同的这条路径。确定性正是重点：相同输入在 GCC 和 Clang 上产出逐字节相同的 PPM，截图测试依赖这一点。

图片（M8a）：`render` 在 `render::DecodePng()` 之后拥有 PNG 解码：已批准的 libpng 依赖封装在
`src/render/image.cpp` 中，像素转换成直通 alpha 的 ARGB `render::Image`，尺寸与像素数在分配前校验，
公开头文件里不出现任何 libpng 类型。`shell` 通过私有加载器（`src/shell/page.h`）拥有页面级策略：
在抓取、解析与样式之后、布局之前，把每个 `<img src>` 相对页面最终 URL 解析，用与页面相同的
`net::Fetch` 抓取（仅 http/file），解码，并维护按 img DOM 节点 id 索引的 `paint::ImageMap` 与
`layout::ReplacedSizes` 映射。缺失或失败的图片被静默跳过，元素按空行内布局（MVP 限制）。
`paint` 把有解码图片的行内 replaced 盒变成 `DrawImage` 命令；显示列表持有
`shared_ptr<const Image>`，因此列表可以比构建它的页面数据活得更久。`render` 用冻结的最近邻整数
源映射、与填充相同的 SrcOver 公式和裁剪栈执行图片。`shell::Browser` 随页面保存图片并绘制它们，
resize 时重新布局而不重新抓取；`--dump-boxes` 使用同一加载器，因此图片几何出现在 dump 中，而
`--dump-dom`/`--dump-style` 保持无图片。

真实字体（M8b）：`render::Font` 在 pimpl 之后封装已批准的 freetype 与 harfbuzz 依赖：公开头文件不出现
freetype/harfbuzz 类型，加载失败是普通错误，字体自己持有 face 字节与 freetype library。度量来自
字体设计单位（`ascent = size * ascender / unitsPerEm`，行高来自 `face->height`），advance 宽度
在请求字号下由 harfbuzz 整形（26.6 求和），`internal::FontRasterizer`（render 私有）把 freetype
8-bit 覆盖率位图按冻结的逐字形整数放置公式画到显示列表基线上，用 SrcOver 覆盖率混合，并裁剪到
当前 clip。有效字号被钳制到 `kMaxRealFontSizePx`（4096 px），对抗性 CSS 无法请求无界字形位图。
会话选择真实字体时，`paint` 为每个文本盒发出一条 `DrawTextWithFont`（基线 = 取整后的盒顶部减去
滚动再加取整后的 ascent），否则发出冻结的位图 `DrawText`；显示列表和图片一样按共享引用持有字体。
`shell` 每次运行解析一次 `--font PATH` 或 `--font-family NAME`（后者通过 fontconfig 的 pattern
匹配，face 0，未知族回退到最佳匹配），加载一个 `Font` 并贯穿页面加载器（`MeasureText` 适配器）、
`Browser` 与绘制。fontconfig 从不离开 shell，字体也从不离开 render 的私有实现。默认仍是内置位图
路径，且它不受影响。

导航（M7）：`shell::Browser` 拥有一个浏览器会话：当前页面（最终 URL、DOM、样式树、盒树、解码后的图片与字体）、
导航历史、整数垂直滚动偏移与绘制。历史是一个 URL 向量加一个索引；每次成功加载追加一条
（后退后再加载会截断前进条目），加载失败则页面、滚动偏移与历史都不变。点击先用滚动偏移换算成
页面坐标，再对盒树做命中测试（`layout::HitTest` 返回绘制顺序中最后一个包含该点的盒），沿 DOM
祖先找到最近的带 `href` 属性的元素，用 `net::ResolveUrl` 相对页面最终 URL 解析后加载。只有
`http`/`file` 引用会导航；只改变 fragment 的引用是 no-op。滚动是垂直的、以整数设备像素表示，
由 `layout::ContentHeight` 钳制在 `[0, content_height - viewport_height]`；`paint` 用视口裁剪
包裹页面并按 `-scroll_y` 平移，`render` 执行冻结的裁剪栈。窗口事件循环把点击、滚轮格数与按键
（Alt+Left/Right 前进后退，方向键/PageUp/PageDown/Home/End 滚动）转发给会话并按需重绘；窗口
标题跟随第一个 `<title>` 文本，没有时回退到 URL。加载在处理函数内同步执行，保持单线程 MVP 模型。页面加载器与图片映射是 shell 的私有实现细节：`toy/shell/browser.h` 保持相同的公开方法，并用前向声明与 out-of-line 析构函数隐藏页面数据。

## 数据模型

- **DOM**：一个用整数 `NodeId` 寻址的 arena（`std::vector<Node>`）；父节点、首/末子节点、前/后兄弟链接都是 id。属性存在每个节点的小 vector 里。没有父/子指针环，没有 `shared_ptr`。
- **样式树**：按 DOM id 索引的计算值组成的并行结构。
- **盒树**：用整数 `BoxId` 寻址的布局 arena（与 DOM 同构）；每个盒引用其 DOM 来源（元素或文本节点 id），并持有一个以 CSS 像素表示的绝对 border-box 矩形。文本盒保存放置在该行上的那个单词。匿名盒没有 DOM 来源。
- **显示列表**：命令的扁平 `std::vector`（`FillRect`、`DrawText`、`DrawImage`、`PushClip`/`PopClip`）；图片命令持有对不可变 `render::Image` 的共享引用，real-font 文本命令持有共享 `render::Font` 并用基线 `y`（位图文本保持冻结的 cell 顶部 `y`、font 为空）。命令使用整数设备像素。
- **单位**：样式/布局中 CSS 像素用 `float`；光栅化器中设备像素用 `int`。MVP 假设 1 CSS px = 1 设备 px（无缩放，无 DPI 缩放）。

选择 arena + id，是因为这些树平凡可析构、在测试中容易 dump，而且不可能形成所有权环。只有当它确实带来痛苦时才重新考虑。

## 线程与事件循环

- 一个进程、一个线程、一个事件循环。MVP 中没有工作线程。`render::Font` 在度量/绘制之间缓存可变的 freetype face 字号，因此刻意只支持单线程；不要跨线程共享同一个 `Font`。
- 页面加载在导航回调中同步执行；加载期间窗口无响应。对玩具项目来说可以接受，只有当真成为问题时才引入异步网络。
- `net::Fetch` 是接口后面的阻塞调用，因此以后可以替换成非阻塞状态机而不触碰调用方。
- 平台事件循环等待窗口事件，并把 close/resize/input/paint 请求转发给 `shell`；由 `shell` 决定重绘什么。重绘按需发生（expose、resize、scroll），不靠定时器。
- 没有全局可变状态：应用对象拥有所有模块。

## 渲染模型

- 只做软件光栅化；没有 GPU，没有渲染线程。
- Surface：32 位 ARGB、straight alpha（非预乘 alpha）、SrcOver 混合、左上角原点。
- 裁剪是由显示列表命令压入的整数矩形栈；初始裁剪是整个 surface，光栅化器的工作量受裁剪区域限制（因此对抗性输入如超大字号也不会让绘制无界）。
- `shell` 在绘制前把 canvas 填白；元素的 `background-color` 绘制为该元素的盒（M6 不会把 `html`/`body` 背景传播到 canvas）。
- 文本默认使用小型内置位图字体，字形为手写的 8x16 ASCII 点阵：字形单元按计算字号做最近邻缩放，没有抗锯齿，非 ASCII 码点渲染 `?` 字形。选择真实字体（`--font`/`--font-family`）时，文本由 harfbuzz 整形、freetype 光栅化为 8-bit 覆盖率，放在字形基线上，以覆盖率为 alpha 与不透明目标做 SrcOver 混合；字形裁剪到当前 clip，有效字号上限 4096 px。位图路径仍是回退路径，其像素冻结。
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
├── AGENTS.md
├── CMakeLists.txt
├── README.md
├── docs/
├── src/
│   ├── main.cpp
│   └── <module>/                    # appears with the module's first real file
│       ├── include/toy/<module>/*.h # public API: #include "toy/<module>/<file>.h"
│       └── *.cpp                    # implementation and private headers
└── tests/
    ├── CMakeLists.txt
    ├── toolchain_smoke.cpp
    └── <module>/                    # one executable per test subject
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
- M8a 图片测试使用像素内容有文档的手工生成 PNG fixtures（`tests/fixtures/images/`）与提交的图片演示站点（`tests/fixtures/img_site/`）：解码器被直接测试，布局用假 replaced 尺寸测试，shell 端到端通过 `shell::Browser` 以及 CLI 的 `--dump-boxes`/`--screenshot` 检查。

## 参考资料

行为首先对照标准、其次对照真实浏览器来检查；两者都只是参考——绝不复制它们的代码：

- HTML Living Standard：<https://html.spec.whatwg.org/multipage/>
- DOM Standard：<https://dom.spec.whatwg.org/>
- URL Standard：<https://url.spec.whatwg.org/>
- CSS Syntax Level 3：<https://www.w3.org/TR/css-syntax-3/>
- Selectors Level 3：<https://www.w3.org/TR/selectors-3/>
- CSS Cascading and Inheritance Level 4：<https://www.w3.org/TR/css-cascade-4/>
- CSS Values and Units Level 3：<https://www.w3.org/TR/css-values-3/>
- HTTP Semantics（RFC 9110）与 HTTP/1.1（RFC 9112）
- PNG（第三版）：<https://www.w3.org/TR/png-3/> 与 libpng 手册（M8a 已批准依赖）：<https://www.libpng.org/pub/png/libpng.html>
- FreeType、HarfBuzz 与 fontconfig 文档（M8b 已链接依赖）：<https://freetype.org/>、<https://harfbuzz.github.io/>、<https://fontconfig.pages.freedesktop.org/fontconfig/>
- Web Platform Tests：<https://github.com/web-platform-tests/wpt>

## 刻意排除的范围

GPU 渲染、多进程架构、沙箱、插件系统、遥测、配置框架、自定义日志框架。功能只在里程碑需要时落地。
