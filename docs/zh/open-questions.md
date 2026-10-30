> 本文为中文翻译，英文原版为准：[open-questions.md](../open-questions.md)。

# 待决问题

仍需用户决定的事项。目前所有已批准内容记录在 [dependencies.md](dependencies.md) 和 [roadmap.md](roadmap.md)。

## 1. HTTPS / TLS —— 用户已推迟

**问题：** 最终是否应支持 `https://`？如果支持，是使用已批准的 TLS 库（例如 OpenSSL），还是完全不支持？

**建议：** 目前不做 TLS：只支持 `http://` 和 `file://`。对玩具项目来说从零实现 TLS 不合比例；当真实页面成为目标时再考虑。OpenSSL 当前未获批准。用户于 2026-10-31 推迟了 TLS（以及代理支持）；注意，代理支持并不能消除这个决定：Google 之类的站点只能通过 `https` 访问。

## 2. 外部测试语料 —— 可选，目前不需要

**问题：** 以后可以下载测试数据（例如 html5lib-tests 或 WPT 子集）作为 golden 文件吗？这些是测试 fixtures，不是运行时依赖。

**建议：** 从手写 fixtures 开始；下载任何东西之前先询问。

## 3. 图像/字体策略 —— M8 开始时

**问题：** 相关库（libpng/zlib、freetype）已获批准，但我们应该使用它们，还是先写自己的最小 PNG/TTF 支持？

**建议：** 先实现最小 PNG 解码器（自写 inflate）和最小 TTF 加载器，因为这是项目的教育核心；如果拖得太久，再回退到已批准的库。
