> 本文为中文翻译，英文原版为准：[open-questions.md](../open-questions.md)。

# 待决问题

仍需用户决定的事项。目前所有已批准内容记录在 [dependencies.md](dependencies.md) 和 [roadmap.md](roadmap.md)。

HTTPS/TLS 问题已于 2026-10-31 解决：用户批准 OpenSSL，M9（最基础 HTTPS）已随之交付（见
[dependencies.md](dependencies.md) 与 [roadmap.md](roadmap.md)）。M12（外部样式表）与 M13（资源
加载管线）都已交付，没有新依赖或新的待决事项（M13 直接链接早已批准的 zlib）。下一计划里程碑是 M14
（CSS 兼容批次 1），随后是 M15（flexbox/position/float 子集）。HTTP 代理（M16）已被用户降低优先级，
不需要新依赖。

## 1. 外部测试语料 —— 可选，目前不需要

**问题：** 以后可以下载测试数据（例如 html5lib-tests 或 WPT 子集）作为 golden 文件吗？这些是测试 fixtures，不是运行时依赖。

**建议：** 从手写 fixtures 开始；下载任何东西之前先询问。
