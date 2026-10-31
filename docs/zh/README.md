> 本文为中文翻译，英文原版为准：[README.md](../README.md)。

# 文档

[顶层 README](../../README.zh.md)（仓库根目录的 `README.zh.md`）面向使用者：浏览器能做什么、如何
构建与运行、演示页面在哪里。本页是更深层内容的索引。

**AI 使用声明：** 本项目大量使用 AI 构建。在依赖代码或文档之前，请先阅读
[AI-DISCLOSURE.md](AI-DISCLOSURE.md)。

## 了解实现

1. [architecture.md](architecture.md) —— 模块划分、数据流、数据模型、渲染与线程模型。
2. [roadmap.md](roadmap.md) —— 里程碑顺序、验收标准与已知限制。
3. [dependencies.md](dependencies.md) —— 第三方依赖策略、审批记录与每个库的用途。

## 参与贡献

1. [CONTRIBUTING.md](CONTRIBUTING.md) —— 构建、测试、代码风格与贡献流程。
2. [conventions.md](conventions.md) —— C++、CMake、测试与文档约定。
3. [decisions.md](decisions.md) —— 已锁定的技术决策及其理由。
4. [SECURITY.md](SECURITY.md) —— 安全模型、测试专用 TLS 密钥与问题报告。

## 当前状态与开发过程

以下内容属于项目的开发记录；它们保持公开，以便审查历史和开发方法。

1. [handoff.md](handoff.md) —— 当前状态、冻结契约、验证步骤与接管指南。
2. [team-workflow.md](team-workflow.md) —— 轮次制开发流程、角色、质量门与经验总结（本项目使用
   AI agent 开发）。
3. [open-questions.md](open-questions.md) —— 维护者仍需决定的问题。
4. [../AGENTS.md](../../AGENTS.md) —— 在本仓库工作的 AI agent 规则。

## 参考

- [assets/](../assets/) —— 顶层 README 使用的截图（由 fixture 页面生成）。
- 中文翻译位于本目录；`docs/` 下的英文文件是唯一权威版本。

## 改动代码前应知道的约定

- 每个模块一个 CMake 库目标，源码在 `src/<module>/`，公开头文件在
  `src/<module>/include/toy/<module>/`。
- 模块目标在根与测试 `CMakeLists.txt` 的显式列表中注册。新模块只在那里加一次；模块 owner 只创建
  自己的 `src/<module>/CMakeLists.txt`。
- 可执行文件通过 CTest 注册；每个模块保持确定性、无需显示器的测试。
- 提交的文件使用英文（只有 `README.zh.md`、`docs/zh/**` 与未来的 i18n 数据例外）。
- 默认构建是 `RelWithDebInfo`；本地配置与临时文件留在 gitignored 的 `.tmp/`，永不进入仓库。
