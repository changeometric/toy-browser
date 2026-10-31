# toybrowser

一个从零实现的玩具网页浏览器：C++20、CMake，不使用现成的浏览器组件。

**状态：M8b —— 真实字体与 PNG 图片。** 窗口与 `--headless --screenshot` 渲染可选 URL 指定的
页面，包括通过已批准的 libpng 依赖抓取并解码的 `<img src="...png">` 图片，以及在用
`--font PATH` 或 `--font-family NAME`（已批准的 freetype/harfbuzz/fontconfig）选择真实字体时
绘制的抗锯齿 TTF 文本；不传字体标志时仍使用内置位图字体，M8b 之前的输出逐字节不变。在窗口中
链接可点击、滚轮与键盘可滚动、Alt+Left/Right 在历史中前进后退，窗口标题跟随 `<title>`。仓库中的
演示站点：[`tests/fixtures/font_site/`](tests/fixtures/font_site/)（真实字体 + 图片）、
[`tests/fixtures/img_site/`](tests/fixtures/img_site/)（图片）与
[`tests/fixtures/nav_site/`](tests/fixtures/nav_site/)（导航）。`--dump-*` 模式仍是主要的 headless
调试路径。详见[路线图](docs/zh/roadmap.md)。英文原版：[README.md](README.md)。

**新团队 / 新 agent？** 先读[接管指南](docs/zh/handoff.md)（当前状态与接手方式），再看
[文档索引](docs/zh/README.md)。

## 构建与运行

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure

./build/toybrowser file:///path/page.html            # 窗口：点击链接、滚轮/键盘滚动、Alt+Left/Right
./build/toybrowser                                   # 打开窗口显示占位页
./build/toybrowser --headless --screenshot out.ppm file:///path/page.html  # 同一页面，无需显示器
./build/toybrowser --size 320x600 "file://$PWD/tests/fixtures/font_site/index.html" \
    --font tests/fixtures/fonts/toy_test.ttf   # 真实 TTF 文本 + 图片演示（fixture 字体）
./build/toybrowser --size 400x200 --font-family "DejaVu Sans" \
    "file://$PWD/tests/fixtures/nav_site/index.html"   # 任意已安装字体族
./build/toybrowser --size 320x600 "file://$PWD/tests/fixtures/img_site/index.html"   # 图片演示
./build/toybrowser "file://$PWD/tests/fixtures/nav_site/index.html"  # 提交的导航演示站点
./build/toybrowser --dump-body file:///etc/hostname  # 抓取 URL，原始字节输出到 stdout
./build/toybrowser --dump-dom  file:///etc/hostname  # 抓取并输出解析后的 DOM 树
./build/toybrowser --dump-style file:///etc/hostname # 抓取、计算样式并输出样式树
./build/toybrowser --dump-boxes file:///etc/hostname # 抓取、计算样式、布局并输出盒树
```

窗口后端链接 libX11，图片解码链接 libpng，真实字体渲染链接 freetype 与 harfbuzz，字体族查找链接
fontconfig（全部已批准，见[依赖说明](docs/zh/dependencies.md)），因此即使只用 headless 模式，构建
时仍然需要这些开发头文件。headless 运行不需要显示器，
是测试和调试的主要模式：所有渲染结果都能不开窗口检查。

CMake 3.20 或更新版本与任何 C++20 编译器都能构建；仓库不依赖某台机器的路径、工具链版本或
已安装的包集合。

## 文档

| 文档 | 内容 |
| --- | --- |
| [handoff.md](docs/zh/handoff.md) | 当前状态、验证步骤、下一里程碑、接管指南 |
| [README.md](docs/zh/README.md) | 文档索引与阅读顺序 |
| [architecture.md](docs/zh/architecture.md) | 模块划分、数据流、数据模型、渲染与线程模型 |
| [decisions.md](docs/zh/decisions.md) | 已锁定的技术决策及其理由 |
| [roadmap.md](docs/zh/roadmap.md) | 里程碑，每个都是最小垂直切片 |
| [dependencies.md](docs/zh/dependencies.md) | 第三方依赖策略与审批记录 |
| [team-workflow.md](docs/zh/team-workflow.md) | 轮次制团队流程：角色、阶段、质量门与经验 |
| [conventions.md](docs/zh/conventions.md) | C++、CMake 与测试约定 |
| [open-questions.md](docs/zh/open-questions.md) | 等待用户决策的问题 |
| [AGENTS.md](AGENTS.md) | AI agent 工作规则 |

表格中的设计文档均有中文翻译（索引见 [docs/zh/README.md](docs/zh/README.md)）；英文原版位于 `docs/`
下同名文件，是唯一权威版本。

## 依赖

目前链接了五个第三方包（libX11、libpng + zlib、freetype、harfbuzz、fontconfig）；浏览器引擎
本身没有任何现成组件（没有 HTML 解析器、CSS 引擎、JS 引擎、布局或渲染引擎）。

| 组件 | 用途 | 许可证 | 状态 |
| --- | --- | --- | --- |
| C++20 标准库 + POSIX | 基础（容器、socket、文件） | 标准 / 系统 | 始终 |
| libX11 | 窗口、输入、像素呈现 | MIT/X11 | 自 M1 起链接 |
| libpng + zlib | PNG 图片解码 | libpng 许可证 / zlib 许可证 | 自 M8a 起链接 |
| freetype | 字体度量与字形光栅化 | FreeType 许可证 | 自 M8b 起链接 |
| harfbuzz | OpenType 文本整形 | MIT | 自 M8b 起链接 |
| fontconfig | 已安装字体的发现与匹配 | MIT 风格 | 自 M8b 起链接 |

依赖策略、审批记录与自写替代方案见 [dependencies.md](docs/zh/dependencies.md)。

## 许可证

MIT —— 见 [LICENSE](LICENSE)。所有已批准的依赖都使用与 MIT 兼容的宽松许可证。

## 基本原则

- C++20 与 CMake，目标平台为 Linux 桌面。
- 不使用现成的 HTML 解析器、CSS 引擎、JS 引擎、布局引擎或浏览器内核：这些由本项目自己实现。
- 底层库在使用前必须获得用户明确批准；“从零”仅指浏览器功能部分，因此已批准的编解码与字体
  机制可以使用。
- 每个里程碑只交付最小可用、可验证的切片——不做超前设计。
