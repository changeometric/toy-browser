> 本文为中文翻译，英文原版为准：[conventions.md](../conventions.md)。

# 约定

小而无聊、保持一致。只保留让代码库统一的规则——不为风格而风格。

## 语言与格式

- C++20。不用编译器扩展；不引入第三方库（只用标准库与已批准的 OS 接口）。
- 格式由提交的 `.clang-format` 定义（基于 Google 风格、100 列）。格式化你碰到的文件；绝不重新格式化无关代码。
- 文件：`snake_case.h`、`snake_case.cpp`，用 `#pragma once` 而不是 include guard。
- 命名：类型 `UpperCamel`，函数/方法 `UpperCamel`，变量 `snake_case`，数据成员 `trailing_underscore_`，常量 `kUpperCamel`。
- 命名空间：`toy`，然后是模块（`toy::dom`、`toy::render`、...）。头文件中不用 `using namespace`。
- 公共头文件位于 `src/<module>/include/toy/<module>/<file>.h`，以 `"toy/<module>/<file>.h"` 形式包含。模块中其他一切均为私有。
- 包含顺序：本翻译单元的头文件、C 头文件、C++ 头文件、项目头文件。
- 小文件和小函数。每个文件一个清晰职责。

## 文档语言

- 权威规则在 [AGENTS.md](../../AGENTS.md)：`README.zh.md`、`docs/zh/**` 以及未来的 i18n 数据可以是非英语；其他一切，包括代码、注释和提交信息，都是英语。先更新英语，并把它当作唯一权威版本。
- 团队内部沟通（任务板、成员间消息、给 Lead 的报告）可以使用任何高效的语言；建议使用英语。面向用户的对话遵循用户自己的语言偏好，该偏好不保存在仓库中。

## 错误

- 可恢复失败返回 `std::optional<T>` 或 `bool` 加人类可读的 `std::string* error`（详见 `docs/architecture.md`）；控制流不用异常。
- 不跨模块边界抛异常；普通控制流不用异常。
- 内部不变量在 debug 构建中用 `assert`；程序员错误应当大声失败。

## 内存

- `std::unique_ptr` 和值类型拥有内存；引用与裸指针从不拥有。
- 树使用以整数 id 寻址的 arena（见 `docs/architecture.md`）。除非确实存在两个所有者，否则不用 `shared_ptr`。

## 模块与 CMake

- 每个模块一个静态库目标：`toy_<module>`；`toybrowser` 可执行文件链接 `toy_shell`。
- 绝不依赖更高层。新源文件在 CMake 中显式列出——不做 glob。
- 每个第一方目标都获得 `toybrowser_set_warnings()`。
- 模块拥有自己的 `CMakeLists.txt`。根与测试 `CMakeLists.txt` 包含显式的模块列表；Lead 添加一次新模块名，目录尚不存在的名字会被跳过。这让共享文件在并行工作期间由 Lead 拥有。
- 共享构建文件（根 `CMakeLists.txt`、`tests/CMakeLists.txt`）和 `docs/` 在并行工作期间由 Lead 拥有；模块所有者不编辑它们。

## 测试

- 每个主题一个文件：`tests/<module>/<topic>_test.cpp`；CTest 名称 `<module>.<topic>`。
- 确定性：无网络、无显示器、不依赖时间、locale、家目录或已安装字体。
- 解析器和布局优先使用 golden dump 比较。新测试使用 header-only 的 `tests/support/check.h`（CMake 接口目标 `toy_test_support`），不再复制本地 `Check`/`g_failures` 测试支架；旧文件在被改动时迁移。不引入测试框架依赖。
- 依赖网络的功能只用进程内 loopback 服务器测试：header-only 的纯 HTTP 服务器 `tests/support/http_server.h`（M12，记录请求以断言重定向/资源用例；M13 起支持持久连接与连接/请求计数）、`net` 测试内部的纯 HTTP 服务器，以及 header-only 的 TLS 服务器 `tests/support/tls_server.h`（提交的仅测试用证书在 `tests/fixtures/tls/`，信任通过 CTest `ENVIRONMENT` 属性提供）。任何测试都不访问公网或使用已安装证书库。
- 二进制或不易理解的 fixture 要把生成器一起提交在它旁边（`tests/fixtures/<kind>/generate*.py`）：只用
  Python 标准库、英文、路径相对脚本目录、输出确定，并在 README 里记录重新生成命令与测试依赖的属性。
  重新生成必须复现已提交内容（字体 fixture 逐字节相同；PNG 字节可能随本地 zlib 变化），否则须在同一
  改动中更新 README 与测试。
- 依赖字体的功能用 `tests/fixtures/fonts/` 中提交的自造 fixture 与生成的 fontconfig 配置测试；测试通过 CTest `ENVIRONMENT` 属性设置 `FONTCONFIG_FILE`：fixture 配置用于自动选择/回退测试，空配置（无字体目录）用于强制内置位图路径。任何测试都不使用已安装字体或机器字体配置。CJK 布局/绘制测试使用 `tests/fixtures/cjk_site/` 与 `layout.cjk_line_break` 分词器测试。
- 没有回归测试的 bug 修复不算完成，除非用户明确豁免。

## 构建

```sh
# 快速构建浏览器本体：测试是 opt-in 的，因此不会编译 74 个测试可执行文件。
# 未指定 CMAKE_BUILD_TYPE 时默认使用 RelWithDebInfo（M11）；调试时传
# -DCMAKE_BUILD_TYPE=Debug。
cmake -S . -B build
cmake --build build

# 完整验证：同时构建并运行测试。
cmake -S . -B build -DTOYBROWSER_BUILD_TESTS=ON -DTOYBROWSER_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

- 只做 out-of-source 构建。加 `-DTOYBROWSER_WERROR=ON` 把警告视为错误。
- `CMakePresets.json` 提供 `default`（只构浏览器）与 `dev`（测试 + warnings-as-errors）；CI 用 GCC
  与 Clang 运行同样的 configure/build/test 序列。presets 只是便捷方式——上面的显式 `cmake` 命令是
  唯一事实来源，必须保持可用。
- 默认构建类型是 `RelWithDebInfo`；不要移除这个默认值（未优化的默认构建会让页面慢数倍），也不要
  移除 `-ffp-contract=off`（它让浮点布局几何在不同构建类型与 GCC/Clang 间逐字节一致）。
- 构建目录从不提交。
- `CMAKE_EXPORT_COMPILE_COMMANDS` 已开启，因此 `compile_commands.json` 会出现在配置的构建目录中，可用于 clangd 和 IDE；构建目录保持未跟踪。
- `cmake --install` 会把可执行文件与 README/LICENSE 安装到标准 `bin/` 与 `share/doc/<project>/`；
  保持安装规则可用，并由 CI 的安装冒烟步骤覆盖。`docs/assets/` 下的截图只是文档（由提交的 fixture
  页面生成），不是测试断言。

## 日志与诊断

- 少量小型日志辅助函数，不用日志框架。库代码不把页面输出打印到 stdout；CLI 的 `--dump-*` 标志负责输出。
- 诊断输出到 stderr；测试预期比较 dump 输出，而不是日志。

## 提交

- 英语、祈使句。前缀如 `feat:`、`fix:`、`docs:`、`build:`、`test:`、`refactor:`。
- 把相关改动合并成一个连贯提交，而不是许多零碎提交：类似或细粒度的编辑应放在一起。不要把无关工作混进同一个提交。
- 未经用户同意，不要重写已发布的历史。
