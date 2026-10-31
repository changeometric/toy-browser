> 本文为中文翻译，英文原版为准：[AI-DISCLOSURE.md](../../AI-DISCLOSURE.md)。

# AI 使用声明

**本项目大量使用 AI 构建。** 代码、测试、fixture、文档与提交消息中的很大一部分，是由大语言模型
编码 agent 在结构化的团队工作流下生成的。人类维护者负责设定目标与里程碑范围、批准第三方依赖、
审查改动，并对最终提交的内容负责。

这份声明刻意放在显眼位置：在依赖代码、文档或仓库中的任何说法之前，请先阅读它。

## 这意味着什么

- 这不是一份由人类独立撰写的参考实现，而是一个 AI 辅助的学习项目；它是玩具浏览器，不是生产浏览器。
- AI 生成的代码与文字可能自信地出错、不完整或过时。重要的说法请对照源码、测试与当前文档核实，不要
  只相信摘要。
- 项目认真做验证：确定性 CTest、GCC 与 Clang 无警告构建、CI、字节兼容门禁与人工审查。这能减少错误，
  但不能覆盖一切；欢迎独立审查。
- 贡献会在同样的背景下被审查。见 [CONTRIBUTING.md](CONTRIBUTING.md)。
- 代码以 [MIT 许可证](../../LICENSE) 提供，不附带担保。

## 工作如何组织

仓库公开记录开发过程：里程碑与验收标准在 [roadmap.md](roadmap.md)，设计记录在
[architecture.md](architecture.md)，已锁定的决策在 [decisions.md](decisions.md)，轮次制开发流程在
[team-workflow.md](team-workflow.md)；给 AI agent 的规则在 [AGENTS.md](../../AGENTS.md)。

如果你认为某个改动或说法有误，请开 issue 或 pull request 并附上复现——这是这类项目能收到的最有用的
审查。
