> 本文为中文翻译，英文原版为准：[architecture.md](../architecture.md)。

# 架构

面向 MVP 的设计：让每个里程碑跑通的最薄实现。这里没有任何推测性内容——抽象只在真实切片需要时才加入。

## 模块划分

| 模块 | 职责 | 可依赖 | 首次需要 |
| --- | --- | --- | --- |
| `base` | 小型共享工具（几何、日志、UTF-8），只在出现需要时才存在 | — | 首次需要时 |
| `platform` | 窗口、输入事件、事件循环、呈现像素表面 | `base` | M1 |
| `render` | 显示列表、软件光栅化器、内置位图字体（度量与字形）、图像解码 | `base` | M1 |
| `net` | URL 解析、socket、HTTP/1.1 抓取 | `base` | M2 |
| `dom` | HTML tokenizer（词法分析器）、树构建器、DOM arena（区域分配器） | `base` | M3 |
| `css` | CSS tokenizer、规则/声明解析器、选择器 | `base` | M4 |
| `style` | 层叠与计算样式、默认样式表 | `dom`、`css` | M4 |
| `layout` | 盒树、块级/行内布局、换行 | `dom`、`style`、来自 `shell` 的文本度量回调 | M5 |
| `paint` | 从盒树构建显示列表（元素背景与文本） | `dom`、`style`、`layout`、`render` | M6 |
| `js` | 小型 JavaScript 引擎与 DOM 绑定（可选，最后） | `dom`、`base` | M9 |
| `shell` | 导航、流水线编排、窗口外壳 | 所有模块 | M1 |

依赖只向下指向；没有环。`render` 完全不了解 HTML/CSS（它消费显示列表）；`paint` 认识 DOM/样式/布局数据但从不光栅化；`platform` 不了解页面内容（它 blit 像素并上报输入）。只有 `shell` 能看到所有模块并把它们接在一起。`base` 尚不存在：M1–M6 的模块只用标准库与标准 POSIX 接口，只有当共享工具确实需要时该模块才出现。

新模块注册在根与 `tests` `CMakeLists.txt` 的显式列表中；Lead 添加一次名字。`--dump-*` 模式与退出码（0 成功，1 运行时错误，2 用法错误）是冻结接口。

## 一次页面加载的完整流程

1. `shell` 收到 URL（CLI 参数或链接点击）。
2. `net` 抓取它：字节、最终 URL、状态。
3. 字节被解码成 UTF-8（MVP：假定 UTF-8，剥掉 BOM；其他编码以后再说）。
4. `dom` 把 HTML 解析成 `Document`。
5. `css` 解析样式表（先支持 `<style>` 和 `style=` 属性；外部 CSS 以后再加）。
6. `style` 把层叠解析成样式树。
7. `layout` 把样式树变成带几何信息的盒树。
8. `paint` 从盒树构建扁平显示列表，`render` 把它光栅化成 `Surface`。
9. `platform` 呈现该表面（窗口 blit，或在 headless 模式下输出 PPM dump）。

MVP 保持整条链路同步、单线程。每个阶段接收普通数据并返回普通数据，因此每个阶段都能在测试中通过 `--dump-*` 命令单独驱动。

网络（M2）：`net` 拥有 URL、socket 和 HTTP。抓取是阻塞式 HTTP/1.1 GET——每个连接一个请求、连接与读取超时、`Content-Length` 或 chunked body、重定向、无 TLS、无 keep-alive——封装在 `Fetch()` 之后，因此以后可以改成非阻塞而不改调用方。`net` 是唯一允许触碰 socket 的模块；页面代码永远看不到 socket。代理支持（先支持 HTTP 代理）计划放在 `FetchOptions` 后面并保持在 `net` 内部；它不需要新依赖。

HTML 与 DOM（M3）：`dom` 拥有 tokenizer、简化树构建器和 arena DOM。解析是容错的，且总是产生一个带 `head` 和 `body` 子节点的 `html` 元素；tokenizer 覆盖支持的子集（元素与属性、注释、doctype、raw-text 与 RCDATA 元素、少量实体），遇到畸形输入也永不失败。`DumpDocument()` 是确定性的文本形式，供 `--dump-dom` 与解析器测试使用。节点用整数 id 寻址（没有持有指针、没有环）；DOM 完全不了解 CSS、布局或脚本。

样式（M4）：`css` 把样式表与声明列表解析成普通结构，完全不了解 DOM。`style` 在 DOM 上匹配这些选择器，应用 UA/作者/行内层叠——依据 specificity（优先级）、`!important` 与继承——并为每个元素生成一个 `ComputedStyle`；内置的 UA 样式表本身就是由我们的解析器解析的 CSS。`DumpStyleTree()` 是 `--dump-style` 与样式测试背后的确定性文本形式。布局只会消费样式树，永远不会消费原始 CSS。

布局（M5）：`layout` 把样式树转换成带绝对 CSS 像素几何的 `BoxTree`。换行需要文本度量，因此 `LayoutDocument()` 接受一个由 `shell` 提供的 `MeasureText` 回调（只用标准类型）；内置等宽位图字体度量位于 `render`，由 `shell` 适配。这样保持依赖方向不变：没有 `layout` → `render` 的边，`layout` 中也不重复字体代码。`DumpBoxTree()` 是 `--dump-boxes` 与布局测试背后的确定性文本形式。布局只消费样式树：它从不读取原始 CSS，也从不绘制。几何是输入的纯函数；MVP 简化的摘要见 [roadmap.md](roadmap.md) 的 known-limits 列表。

绘制（M6）：`paint` 按前序遍历盒树，产出扁平的 `render::DisplayList`：每个计算背景非透明的元素盒变成一条 `FillRect`（背景先于子内容），每个文本盒变成一条 `DrawText`，使用继承的颜色与字号。`paint` 从不光栅化，也从不包含 render 的实现头文件。`render` 执行该列表：矩形填充对不透明目标使用冻结的 SrcOver 整数公式；文本使用内置 8x16 ASCII 字形表，按最近邻单元缩放（无抗锯齿）；整数矩形裁剪栈约束每条命令（M7 会用视口 `PushClip`/`PopClip` 对包裹页面，并把每条命令按滚动偏移平移）。`shell` 把 canvas 填白、按 surface 宽度布局页面、构建列表并光栅化；窗口与 `--screenshot` 共享完全相同的这条路径。确定性正是重点：相同输入在 GCC 和 Clang 上产出逐字节相同的 PPM，截图测试依赖这一点。

导航（M7）：`shell::Browser` 拥有一个浏览器会话：当前页面（最终 URL、DOM、样式树与盒树）、
导航历史、整数垂直滚动偏移与绘制。历史是一个 URL 向量加一个索引；每次成功加载追加一条
（后退后再加载会截断前进条目），加载失败则页面、滚动偏移与历史都不变。点击先用滚动偏移换算成
页面坐标，再对盒树做命中测试（`layout::HitTest` 返回绘制顺序中最后一个包含该点的盒），沿 DOM
祖先找到最近的带 `href` 属性的元素，用 `net::ResolveUrl` 相对页面最终 URL 解析后加载。只有
`http`/`file` 引用会导航；只改变 fragment 的引用是 no-op。滚动是垂直的、以整数设备像素表示，
由 `layout::ContentHeight` 钳制在 `[0, content_height - viewport_height]`；`paint` 用视口裁剪
包裹页面并按 `-scroll_y` 平移，`render` 执行冻结的裁剪栈。窗口事件循环把点击、滚轮格数与按键
（Alt+Left/Right 前进后退，方向键/PageUp/PageDown/Home/End 滚动）转发给会话并按需重绘；窗口
标题跟随第一个 `<title>` 文本，没有时回退到 URL。加载在处理函数内同步执行，保持单线程 MVP 模型。

## 数据模型

- **DOM**：一个用整数 `NodeId` 寻址的 arena（`std::vector<Node>`）；父节点、首/末子节点、前/后兄弟链接都是 id。属性存在每个节点的小 vector 里。没有父/子指针环，没有 `shared_ptr`。
- **样式树**：按 DOM id 索引的计算值组成的并行结构。
- **盒树**：用整数 `BoxId` 寻址的布局 arena（与 DOM 同构）；每个盒引用其 DOM 来源（元素或文本节点 id），并持有一个以 CSS 像素表示的绝对 border-box 矩形。文本盒保存放置在该行上的那个单词。匿名盒没有 DOM 来源。
- **显示列表**：命令的扁平 `std::vector`（今天的 `FillRect`、`DrawText`、`PushClip`/`PopClip`；图像到来时加 `DrawImage`）。命令使用整数设备像素。
- **单位**：样式/布局中 CSS 像素用 `float`；光栅化器中设备像素用 `int`。MVP 假设 1 CSS px = 1 设备 px（无缩放，无 DPI 缩放）。

选择 arena + id，是因为这些树平凡可析构、在测试中容易 dump，而且不可能形成所有权环。只有当它确实带来痛苦时才重新考虑。

## 线程与事件循环

- 一个进程、一个线程、一个事件循环。MVP 中没有工作线程。
- 页面加载在导航回调中同步执行；加载期间窗口无响应。对玩具项目来说可以接受，只有当真成为问题时才引入异步网络。
- `net::Fetch` 是接口后面的阻塞调用，因此以后可以替换成非阻塞状态机而不触碰调用方。
- 平台事件循环等待窗口事件，并把 close/resize/input/paint 请求转发给 `shell`；由 `shell` 决定重绘什么。重绘按需发生（expose、resize、scroll），不靠定时器。
- 没有全局可变状态：应用对象拥有所有模块。

## 渲染模型

- 只做软件光栅化；没有 GPU，没有渲染线程。
- Surface：32 位 ARGB、straight alpha（非预乘 alpha）、SrcOver 混合、左上角原点。
- 裁剪是由显示列表命令压入的整数矩形栈；初始裁剪是整个 surface，光栅化器的工作量受裁剪区域限制（因此对抗性输入如超大字号也不会让绘制无界）。
- `shell` 在绘制前把 canvas 填白；元素的 `background-color` 绘制为该元素的盒（M6 不会把 `html`/`body` 背景传播到 canvas）。
- 文本使用小型内置位图字体，字形为手写的 8x16 ASCII 点阵，因此不需要字体库；字形单元按计算字号做最近邻缩放，没有抗锯齿。非 ASCII 码点暂时渲染 `?` 字形；真实 TTF 文本在审批之后再加。
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
- Web Platform Tests：<https://github.com/web-platform-tests/wpt>

## 刻意排除的范围

GPU 渲染、多进程架构、沙箱、插件系统、遥测、配置框架、自定义日志框架。功能只在里程碑需要时落地。
