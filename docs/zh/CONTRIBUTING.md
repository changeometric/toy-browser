> 本文为中文翻译，英文原版为准：[../../CONTRIBUTING.md](../../CONTRIBUTING.md)。

# 贡献指南

感谢你的关注。这是一个从零实现的玩具浏览器：小而可审查、能保持既有测试通过的改动最受欢迎。

> **AI 使用：** 本仓库大量使用 AI 构建，见 [AI-DISCLOSURE.md](AI-DISCLOSURE.md)。贡献会在这一背景下
> 被审查。

## 开始之前

- 先读 [architecture.md](architecture.md) 了解模块划分与依赖方向。
- 读 [conventions.md](conventions.md) 了解 C++/CMake/测试约定。
- 浏览器引擎部分（HTML/CSS 解析、样式、布局、绘制）在本仓库实现，必须保持如此。现成的浏览器
  组件（HTML/CSS 解析器、JS 引擎、布局或渲染引擎、GUI 框架）不会被接受。
- 允许使用底层库，但每个新的第三方依赖都需要维护者明确批准，并在使用前记录到
  [dependencies.md](dependencies.md)。

## 构建与测试

需要 Linux、CMake 3.20 或更新版本，以及 C++20 编译器。Debian/Ubuntu：

```sh
sudo apt install cmake g++ clang libx11-dev libpng-dev libjpeg-dev libfreetype-dev \
    libharfbuzz-dev libfontconfig1-dev libssl-dev
```

```sh
cmake --preset dev                 # 测试 + warnings-as-errors，构建目录 build-dev/
cmake --build --preset dev
ctest --test-dir build-dev --output-on-failure
```

CI 用 GCC 与 Clang 运行同样的配置；改动应同时通过两者。默认构建类型是 `RelWithDebInfo`；只在
本地调试时用 `-DCMAKE_BUILD_TYPE=Debug`。headless 运行不需要显示器，`--dump-*` 与 `--screenshot`
是主要调试工具。

## 代码与测试

- C++20、CMake，GCC 与 Clang 无警告（本地标准设置是 `TOYBROWSER_WERROR=ON`）。
- 保持模块边界：`render` 不能知道 HTML/CSS，平台层不能知道页面内容，`layout` 通过 shell 提供的
  回调消费文本度量。
- 行为改动要加确定性、无需显示器的测试。测试不能依赖已安装字体、公网、墙钟时间或机器路径；提交的
  fixture 位于 `tests/fixtures/`，二进制 fixture 随附生成器。
- CTest 里不要断言耗时。性能工作用测量验证，并在 PR 中报告。
- 改动的 C++ 文件用 `clang-format` 格式化（仓库自带 `.clang-format`）。

## 文档与语言

- 提交的文件使用英文，例外是 `README.zh.md` 与 `docs/zh/**`（中文翻译）。先更新英文源文件。
- 行为改动要同步更新文档，尤其是 [roadmap.md](roadmap.md) 的已知限制与 [handoff.md](handoff.md)
  的冻结契约。
- 本机、机器相关或个人配置绝不能提交。临时文件放在 gitignored 的 `.tmp/`。

## 提交与 PR

- 一个提交只做一个连贯的改动；不要把无关重构或格式化混进来。
- 说明改了什么、为什么，以及如何验证（命令与结果）。
- 不要重写已发布历史，也不要 force-push 共享分支。
- 请保持尊重与建设性；本项目不容忍骚扰行为。

## 报告问题

开 issue 并附上页面 URL（或最小本地 HTML repro）、使用的命令、预期与实际输出，以及问题是否只出现
在 `--screenshot` 而不在 `--dump-boxes`。安全敏感问题见 [SECURITY.md](SECURITY.md)。

## 许可证

提交贡献即表示你同意按仓库的 [MIT 许可证](../../LICENSE) 授权你的贡献。
