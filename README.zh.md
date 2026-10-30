# toybrowser

一个从零实现的玩具网页浏览器：C++20、CMake，不使用现成的浏览器组件。

**状态：M2 —— 通过 headless 的 `--dump-body` 命令获取 URL（`http://` 与 `file://`）。** 页面渲染仍是
M1 的占位页；HTML 解析从 M3 开始。详见[路线图](docs/roadmap.md)。英文原版：[README.md](README.md)。

## 构建与运行

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure

./build/toybrowser                                   # 打开窗口显示占位页
./build/toybrowser --headless --screenshot out.ppm   # 同一帧，无需显示器
./build/toybrowser --dump-body file:///etc/hostname  # 抓取 URL，原始字节输出到 stdout
```

窗口后端在构建时需要 libX11（已批准，见[依赖说明](docs/dependencies.md)）。headless 路径既不需要
显示器，也不需要额外库，是测试和调试的主要模式：所有渲染结果都能不开窗口检查。

## 文档

| 文档 | 内容 |
| --- | --- |
| [architecture.md](docs/architecture.md) | 模块划分、数据流、数据模型、渲染与线程模型 |
| [roadmap.md](docs/roadmap.md) | 里程碑，每个都是最小垂直切片 |
| [dependencies.md](docs/dependencies.md) | 第三方依赖策略与审批记录 |
| [conventions.md](docs/conventions.md) | C++、CMake 与测试约定 |
| [team-workflow.md](docs/team-workflow.md) | 轮次制团队流程：角色、阶段、质量门与经验 |
| [open-questions.md](docs/open-questions.md) | 等待用户决策的问题 |
| [AGENTS.md](AGENTS.md) | AI agent 工作规则 |

## 依赖

目前只链接了一个第三方库；浏览器引擎本身没有任何现成组件（没有 HTML 解析器、CSS 引擎、JS 引擎、
布局或渲染引擎）。

| 组件 | 用途 | 许可证 | 状态 |
| --- | --- | --- | --- |
| C++20 标准库 + POSIX | 基础（容器、socket、文件） | 标准 / 系统 | 始终 |
| libX11 | 窗口、输入、像素呈现 | MIT/X11 | 自 M1 起链接 |
| libpng + zlib | 图片解码（M8，如使用） | libpng 许可证 / zlib 许可证 | 已批准，未链接 |
| freetype | 字体光栅化（M8，如使用） | FreeType 许可证 | 已批准，未链接 |

依赖策略、审批记录与自写替代方案见 [dependencies.md](docs/dependencies.md)。

## 许可证

MIT —— 见 [LICENSE](LICENSE)。所有已批准的依赖都使用与 MIT 兼容的宽松许可证
（libX11 为 MIT/X11；libpng/zlib/freetype 各有自己的宽松许可证）。

## 基本原则

- C++20 与 CMake，目标平台为 Linux 桌面。
- 不使用现成的 HTML 解析器、CSS 引擎、JS 引擎、布局引擎或浏览器内核：这些由本项目自己实现。
- 底层库在使用前必须获得用户明确批准。
- 每个里程碑只交付最小可用、可验证的切片——不做超前设计。
