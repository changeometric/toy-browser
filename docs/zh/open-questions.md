> 本文为中文翻译，英文原版为准：[open-questions.md](../open-questions.md)。

# 待决问题

仍需用户决定的事项。目前所有已批准内容记录在 [dependencies.md](dependencies.md) 和 [roadmap.md](roadmap.md)。

HTTPS/TLS 问题已于 2026-10-31 解决：用户批准 OpenSSL 用于计划中的 M9 HTTPS 里程碑（见
[dependencies.md](dependencies.md) 与 [roadmap.md](roadmap.md)）；HTTP 代理计划为 M10，不需要新依赖。

## 1. 外部测试语料 —— 可选，目前不需要

**问题：** 以后可以下载测试数据（例如 html5lib-tests 或 WPT 子集）作为 golden 文件吗？这些是测试 fixtures，不是运行时依赖。

**建议：** 从手写 fixtures 开始；下载任何东西之前先询问。
