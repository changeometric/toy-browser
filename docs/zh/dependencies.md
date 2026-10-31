> 本文为中文翻译，英文原版为准：[dependencies.md](../dependencies.md)。

# 依赖

策略本身在 [AGENTS.md](../../AGENTS.md)。本文件记录实际允许什么、还在申请什么。

## 当前清单

**目前链接了六个第三方包：**

- **libX11（MIT/X11）**，用于 X11 窗口后端（M1）。
- **libpng + zlib（libpng 许可证 / zlib 许可证）**，由 `render` 用于解码 PNG 图片（M8a）。
  通过 CMake 的 `find_package(PNG REQUIRED)` 发现；zlib 由 libpng 一并引入。
- **freetype（FreeType 许可证）**，由 `render` 用于真实字体的度量与字形光栅化（M8b）。通过
  `find_package(Freetype REQUIRED)` 发现。
- **harfbuzz（MIT）**，由 `render` 用于 OpenType 文本整形（M8b）。通过
  `find_package(harfbuzz CONFIG REQUIRED)` 发现。
- **fontconfig（MIT 风格）**，由 `shell` 在 `--font-family` 渲染时用于已安装字体的发现与匹配
  （M8b）。通过 `find_package(Fontconfig REQUIRED)` 发现。
- **OpenSSL（libssl + libcrypto，Apache-2.0）**，由 `net` 用于 TLS/`https://` 抓取（M9）。通过
  `find_package(OpenSSL REQUIRED)` 发现并 PRIVATE 链接到 `toy_net`；证书校验始终开启并基于默认
  信任库，没有 insecure 模式。

其他一切都是 C++20 标准库和标准 POSIX 接口。没有使用 HTML 解析器、CSS 引擎、JS 引擎、布局引擎或
浏览器引擎——那些都在本仓库中编写。

## 当前已批准

用户于 2026-10-31（M1 开始时）批准：

- **libX11**——Linux 上的窗口、输入与像素呈现，自 M1 起使用。许可证：MIT/X11。通过 CMake 的
  `find_package(X11)` 链接；不做 vendoring。引入于提交 `0572b8b`（M1）。
- **libpng + zlib**——图片解码，自 M8a 起使用。许可证：libpng 许可证与 zlib 许可证。通过
  `src/render/CMakeLists.txt` 中的 `find_package(PNG REQUIRED)` 链接；引入于 M8a 轮。
- **freetype**——真实 TTF 文本的字体度量与字形光栅化，自 M8b 起使用。许可证：FreeType 许可证。
  通过 `src/render/CMakeLists.txt` 中的 `find_package(Freetype REQUIRED)` 链接。
- **libxcb**——允许作为替代窗口库；当前未使用。

用户于 2026-10-31（M8a 开始前）批准，并在 M8b 链接：

- **harfbuzz**——真实字体文本的 OpenType 整形（按字体选择并定位字形）。许可证：MIT。通过
  `src/render/CMakeLists.txt` 中的 `find_package(harfbuzz CONFIG REQUIRED)` 链接。
- **fontconfig**——已安装字体的发现与匹配，由 shell 用于把 `--font-family NAME` 解析为字体文件。
  许可证：fontconfig 的 MIT 风格许可证。通过 `src/shell/CMakeLists.txt` 中的
  `find_package(Fontconfig REQUIRED)` 链接。

用户于 2026-10-31 批准，用于计划中的 M9 HTTPS 里程碑：

- **OpenSSL（libssl + libcrypto）**——`https://` 抓取所需的 TLS 1.2/1.3。许可证：Apache-2.0
  （OpenSSL 3.x）。通过 CMake 的 `find_package(OpenSSL REQUIRED)` 发现并 PRIVATE 链接到
  `toy_net`；不做 vendoring。使用时证书与主机名校验始终开启，使用 OpenSSL 默认/系统信任库（标准
  `SSL_CERT_FILE`/`SSL_CERT_DIR` 覆盖仍有效），且没有 insecure 模式。

其他什么都没链接。C++20 标准库和标准 OS/POSIX 接口仍是默认。所有已批准的候选库都使用与项目
MIT 许可证兼容的宽松许可证。

## “从零”的范围

用户于 2026-10-31 澄清：“从零”适用于**浏览器功能**——网络、HTML/CSS 解析、布局、绘制与
JavaScript——而不包括底层编解码与字体机制。因此图片解码与字体光栅化/整形/发现使用上面已批准的
库，而不是自写实现。浏览器引擎部分仍然由我们自己编写。

## 系统接口仍然是依赖

任何与显示服务器通信的东西（例如 X11 客户端库）在本项目中都算第三方依赖，需要审批。libX11 现在
已获批用于窗口；headless 后端——渲染到内存并写 PPM 文件——让整条流水线在无显示器时可测试。

## 依赖候选

| 候选 | 用途 | 自写替代方案 | 粗略成本 | 状态 |
| --- | --- | --- | --- | --- |
| libX11 | 窗口、输入、呈现像素 | 自己实现 X11 线协议 | 中等；协议怪癖、XShm 回退 | **已批准，自 M1 起在用**（先用普通 `XPutImage`；需要时再上 XShm） |
| libxcb | 同上，API 更现代 | 同上 | 中等 | **已批准，未使用**（libX11 的替代） |
| libpng + zlib | PNG 解码 | 自写 inflate + PNG filters/decoder | 约 600–900 行 | **已批准，自 M8a 起链接**（由 `render::DecodePng` 使用） |
| freetype | TTF 字形光栅化与度量 | 自写子集 TTF 加载器（cmap/glyf/hmtx，无 hinting） | 中等 | **已批准，自 M8b 起链接**（由 `render::Font` 使用） |
| harfbuzz | OpenType 整形（连字、字形选择、定位） | 只做基础拉丁整形 | 高 | **已批准，自 M8b 起链接**（由 `render::Font` 使用） |
| fontconfig | 已安装字体的发现与匹配 | 硬编码字体路径 / 不做发现 | 中等 | **已批准，自 M8b 起链接**（shell 的 `--font-family` 使用） |
| OpenSSL | TLS，即 `https://` | 从零实现不现实 | 高 | **2026-10-31 已批准，自 M9 起链接**（始终校验证书与主机名、使用默认信任库） |
| stb_truetype / stb_image | 单头文件即插即用实现 | 自写实现 | 低 | **未批准**——与本项目的精神冲突 |
| icu | Unicode 数据与完整国际化 | 先做小型 ASCII/拉丁子集 | 高 | **不计划** |

今天只链接了上面列出的六个包。任何其他行都需要先完成下面的流程。

## 审批流程

1. Agent 说明：用途、许可证、规模与维护负担，以及自写替代方案。
2. 用户回答是或否。
3. 若是：在本文件记录库、包/版本、日期以及引入它的提交；通过 CMake（`find_package` 或
   `pkg-config`）链接，除非用户要求，否则不做 vendoring。
4. 如果机器上缺少该包，请用户安装——agent 自己从不安装任何东西。
5. 若否：实现自写替代方案，或推迟该功能。

## 保持代码树可移植的规则

- 代码或 CMake 中不使用绝对路径，不硬编码库版本或包位置。
- 库只能通过 CMake find 模块或 pkg-config 发现，且必须在审批之后。
- 可选功能必须能优雅降级：没有可选依赖时测试也能通过。headless 运行不需要显示器，但当前二进制
  仍然链接 libX11（窗口后端总是会被构建）、libpng（图片解码）、freetype 与 harfbuzz（真实字体
  渲染，属于 `render`）以及 fontconfig（shell 的 `--font-family` 路径）。这些是必需的构建依赖；
  构建需要它们的开发头文件。
- 不要假设某台机器上有什么字体、窗口管理器、GPU 或包。测试使用提交的自造字体 fixture
  （`tests/fixtures/fonts/`），从不使用已安装字体；计划中的 M9 TLS 测试使用提交的自签测试证书，
  从不使用公共 CA 或公共站点。
