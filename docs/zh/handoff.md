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
| 审阅轮 | 修复无上限的 `--size`、文件 body 上限、dump 缩进；20 个 CTest 测试 |

尚未实现：布局、页面绘制、真实文本/字体、图像、JavaScript、`https`/TLS、代理、外部样式表、交互与导航。窗口模式显示 M1 的占位页；今天所有有用的东西都通过 headless CLI 模式。

## 验证当前状态（无需上下文）

以下命令请在仓库根目录执行：

```sh
BUILD=build/onboard-check
cmake -S . -B "$BUILD" -DTOYBROWSER_WERROR=ON
cmake --build "$BUILD"
ctest --test-dir "$BUILD" --output-on-failure

"$BUILD"/toybrowser --help
"$BUILD"/toybrowser --dump-dom   "file://$PWD/README.md" | head
"$BUILD"/toybrowser --dump-style "file://$PWD/README.md" | head
"$BUILD"/toybrowser --headless --screenshot /tmp/toybrowser.ppm && file /tmp/toybrowser.ppm
```

预期：20/20 测试通过；使用 `-DTOYBROWSER_WERROR=ON` 构建在 GCC 和 Clang 上无警告；dump 输出确定性的树；截图写出二进制 PPM。所有 headless 模式都不需要显示器。需要 CMake 3.20 或更新版本；构建需要 X11 开发头文件（已批准的依赖）；只有窗口模式需要运行中的显示服务器。

## 下一轮：M5 —— 布局（尚未开始）

- 目标：把样式树变成带几何信息的盒树，由 `--dump-boxes` dump。
- 初步范围：块级与行内盒、匿名盒、宽度/margin/padding、换行。border 不在 M4 属性集中，M5 范围之外。
- 文本度量：`layout` 接受由 `shell` 提供的度量回调；内置位图字体位于 `render`。位图字体决策已锁定（TTF 在 M8）；M5 intake 只确定回调 API 与 ASCII 覆盖范围。
- `layout` 已经在共享 CMake 模块列表中注册，因此有 `src/layout/CMakeLists.txt` 就能开始。
- 验收草案：fixture 页面产生预期的盒 dump；所有现有测试保持通过；两个编译器下无警告；完全 headless。

M5 之后的 backlog（细节见 [roadmap.md](roadmap.md)）：M8 的图像/字体库决策、代理支持、TLS，以及测试辅助代码合并。

## 仓库地图

- `src/<module>/` —— 每个模块一个 CMake 库目标（`base` 尚不存在；模块图与依赖方向见 [architecture.md](architecture.md)）。
- `src/main.cpp` —— 只做 CLI 解析；`shell` 编排 fetch → parse → style → dump/present。
- `tests/<module>/` —— 每个主题一个可执行文件，注册到 CTest。
- `docs/` —— 架构、路线图、约定、团队流程、决策、依赖、待决问题（本文件是快照）。
- 共享任务板 —— 由 agent 运行时提供（任务 create/claim/update，带 revision）；如果运行时没有，则用 `docs/tasks/<milestone>.md`，字段相同。见 [team-workflow.md](team-workflow.md)。
- `README.md` / `README.zh.md` 与 `docs/zh/` —— 面向用户的入口（英语为权威）。

## 冻结契约（不要随意改动）

- CLI 模式、标志与退出码：`0` 成功，`1` 运行时错误，`2` 用法错误。
- Dump 格式（DOM、style、以后的 boxes）：确定性、面向行、由测试断言；任何改动都需要同一轮内更新测试和文档。
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
- 深层嵌套输入曾让 dump 变成二次复杂度；任何新的 dump 都要保持有深度上限并有测试。
- 构建产物与目标文件绝不能提交；使用隔离的构建目录。
- 还没有 `https`：大多数真实站点无法抓取；`http://` 和 `file://` 可用。
- HTML/CSS/URL 解析器按设计是容错的；它们遇到畸形输入永不失败，所以“没有报错”不是正确性的证据——把 dump 与 golden 测试比较。
