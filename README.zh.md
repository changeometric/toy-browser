# toybrowser

一个从零实现的玩具网页浏览器：C++20、CMake，不使用现成的浏览器组件。

**状态：M11 —— 性能。** 浏览器可抓取 `http://`、`https://` 与 `file://` 页面（`https://` 使用
已批准的 OpenSSL，证书与主机名校验始终开启，没有 insecure 模式），并可在窗口或 headless 模式下
绘制页面。M11 在不改变任何像素的前提下让大页面响应流畅：默认构建改为优化构建；真实字体通过有界
缓存做整形/光栅化；绘制按视口裁剪；窗口循环不再对每次 Expose/resize 事件重排（压力页实测：单帧
10.6 s -> 3.5 ms，布局 2.1 s -> 31 ms）。渲染仍是软件、单线程；刻意不使用 GPU 后端。不传任何字体标志时，浏览器像普通浏览器一样自己选择字体：计算出的 CSS
`font-family`/`font-weight`/`font-style` 通过已批准的 fontconfig 库匹配（初始字体族 `serif`），
首选字体缺少的字符回退到覆盖它的已安装字体。布局把文本切成 run，并给 CJK 文本独立的断行单元，
因此没有空格的中文页面也能阅读、正常换行。`--font PATH` 与 `--font-family NAME` 仍是显式覆盖；
没有可用系统字体时退化到内置位图字体。真实文本抗锯齿（freetype + harfbuzz），
`<img src="...png">` 图片由 libpng 解码，窗口中链接可点击、滚轮与键盘可滚动、Alt+Left/Right 在
历史中前进后退、窗口标题跟随 `<title>`。**下一计划里程碑：M12 —— HTTP 代理**（用户已降低优先级；
设计草案见[路线图](docs/zh/roadmap.md)）。仓库中的演示站点：
[`tests/fixtures/cjk_site/`](tests/fixtures/cjk_site/)（自动字体 + 可读中文）、
[`tests/fixtures/font_site/`](tests/fixtures/font_site/)（显式真实字体 + 图片）、
[`tests/fixtures/img_site/`](tests/fixtures/img_site/)（图片）、
[`tests/fixtures/nav_site/`](tests/fixtures/nav_site/)（导航）与
[`tests/fixtures/https_site/`](tests/fixtures/https_site/)（HTTPS pipeline）。`--dump-*` 模式仍是
主要的 headless 调试路径。详见[路线图](docs/zh/roadmap.md)。英文原版：[README.md](README.md)。

**新团队 / 新 agent？** 先读[接管指南](docs/zh/handoff.md)（当前状态与接手方式），再看
[文档索引](docs/zh/README.md)。

## 构建与运行

```sh
# 快速构建浏览器本体（测试是 opt-in 的）。
cmake -S . -B build
cmake --build build

# 完整验证：同时构建并运行测试。
cmake -S . -B build -DTOYBROWSER_BUILD_TESTS=ON -DTOYBROWSER_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure

./build/toybrowser file:///path/page.html            # 窗口：点击链接、滚轮/键盘滚动、Alt+Left/Right
./build/toybrowser                                   # 打开窗口显示占位页
./build/toybrowser --headless --screenshot out.ppm file:///path/page.html  # 同一页面，无需显示器
./build/toybrowser --size 320x600 "file://$PWD/tests/fixtures/font_site/index.html" \
    --font tests/fixtures/fonts/toy_test.ttf   # 真实 TTF 文本 + 图片演示（fixture 字体）
./build/toybrowser --size 400x200 --font-family "sans-serif" \
    "file://$PWD/tests/fixtures/nav_site/index.html"   # 强制某已安装字体族
./build/toybrowser --size 400x240 "file://$PWD/tests/fixtures/cjk_site/index.html"  # 自动字体 + 中文换行
./build/toybrowser --size 800x900 --screenshot /tmp/zh.ppm https://www.gov.cn/  # 真实中文页面（系统字体）
./build/toybrowser --size 320x600 "file://$PWD/tests/fixtures/img_site/index.html"   # 图片演示
./build/toybrowser "file://$PWD/tests/fixtures/nav_site/index.html"  # 提交的导航演示站点
./build/toybrowser --dump-body https://example.com/   # 抓取 https URL（TLS，始终校验）
./build/toybrowser --dump-dom  https://example.com/   # 抓取并解析真实 https 页面
./build/toybrowser --size 1024x768 --screenshot out.ppm https://example.com/  # 绘制 https 页面
./build/toybrowser --dump-body file:///etc/hostname  # 抓取 URL，原始字节输出到 stdout
./build/toybrowser --dump-dom  file:///etc/hostname  # 抓取并输出解析后的 DOM 树
./build/toybrowser --dump-style file:///etc/hostname # 抓取、计算样式并输出样式树
./build/toybrowser --dump-boxes file:///etc/hostname # 抓取、计算样式、布局并输出盒树
```

窗口后端链接 libX11，图片解码链接 libpng，文本渲染/整形链接 freetype 与 harfbuzz，自动字体选择链接
fontconfig，`https://` 链接 OpenSSL（全部已批准，见[依赖说明](docs/zh/dependencies.md)），因此即使
只用 headless 模式，构建时仍然需要这些开发头文件。headless 运行不需要显示器，
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

目前链接了六个第三方包（libX11、libpng + zlib、freetype、harfbuzz、fontconfig、OpenSSL）；
浏览器引擎本身没有任何现成组件（没有 HTML 解析器、CSS 引擎、JS 引擎、布局或渲染引擎）。

| 组件 | 用途 | 许可证 | 状态 |
| --- | --- | --- | --- |
| C++20 标准库 + POSIX | 基础（容器、socket、文件） | 标准 / 系统 | 始终 |
| libX11 | 窗口、输入、像素呈现 | MIT/X11 | 自 M1 起链接 |
| libpng + zlib | PNG 图片解码 | libpng 许可证 / zlib 许可证 | 自 M8a 起链接 |
| freetype | 字体度量与字形光栅化 | FreeType 许可证 | 自 M8b 起链接 |
| harfbuzz | OpenType 文本整形 | MIT | 自 M8b 起链接 |
| fontconfig | 自动字体发现/匹配、语言提示与字符回退 | MIT 风格 | 自 M8b 起链接（自动选择自 M10 起） |
| OpenSSL | `https://` 的 TLS | Apache-2.0 | 自 M9 起链接 |

依赖策略、审批记录与自写替代方案见 [dependencies.md](docs/zh/dependencies.md)。

## 许可证

MIT —— 见 [LICENSE](LICENSE)。所有已批准的依赖都使用与 MIT 兼容的宽松许可证。

## 基本原则

- C++20 与 CMake，目标平台为 Linux 桌面。
- 不使用现成的 HTML 解析器、CSS 引擎、JS 引擎、布局引擎或浏览器内核：这些由本项目自己实现。
- 底层库在使用前必须获得用户明确批准；“从零”仅指浏览器功能部分，因此已批准的编解码与字体
  机制可以使用。
- 每个里程碑只交付最小可用、可验证的切片——不做超前设计。
