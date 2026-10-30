> 本文为中文翻译，英文原版为准：[README.md](../README.md)。

# 文档

新团队或新 agent 的阅读顺序。先读 `../../README.zh.md` 的一段式项目摘要，然后按下面的顺序：

1. [handoff.md](handoff.md) —— 当前状态：已完成什么、如何验证、接下来做什么。
2. [architecture.md](architecture.md) —— 模块划分、数据流、数据模型、分层规则。
3. [roadmap.md](roadmap.md) —— 里程碑、验收标准、已知限制、backlog。
4. [conventions.md](conventions.md) —— C++、CMake、测试与文档约定。
5. [team-workflow.md](team-workflow.md) —— 一轮工作如何计划、执行与交付。
6. [decisions.md](decisions.md) —— 已锁定的技术决策及锁定原因。
7. [dependencies.md](dependencies.md) —— 依赖策略与审批记录。
8. [open-questions.md](open-questions.md) —— 等待用户决策的问题。
9. [zh/](./) —— 中文翻译；英文始终是唯一权威版本。

每个 agent 的硬性规则：[../../AGENTS.md](../../AGENTS.md)。

动代码之前值得了解的现有约定：

- 每个模块一个 CMake 库目标，位于 `src/<module>/`，公共头文件位于 `include/toy/<module>/`。
- 只要 `src/<module>/CMakeLists.txt` 存在，根与测试的 `CMakeLists.txt` 就会包含该模块；模块所有者创建自己的文件，从不编辑共享文件。
- 可执行文件注册到 CTest；每个模块都保持确定性、无需显示器的测试。
- 提交的文件都是英语（只有 `README.zh.md`、`docs/zh/**` 以及未来的 i18n 数据例外）。
