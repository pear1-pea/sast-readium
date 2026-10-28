# Ask PDF 对话系统方案（v2）

本文把 Ask PDF 从「单轮问答」升级为「按文档的对话系统」。v2 在 v1 方案基础上
采纳了架构评审建议：先定数据模型、状态机与任务生命周期，再做 UI。

## 目标

- 每轮回答都有明确的状态、检索证据、引用关系、取消语义和可测试的生命周期。
- 保留现有检索、上下文预算、引用校验、异步生成与 PDF 定位能力。
- 不引入独立的聊天 UI 框架，复用 Qt Widgets 与现有组件。

## 设计约束

1. **状态唯一权威**：`RagDocumentCoordinator` 管理业务状态，
   `RagConversationModel` 只负责向 View 正确通知变化；UI 不自行拼状态。
2. **回调必须可失效**：`documentHash + turnId + generation` 三重校验；
   `cancel` 不能替代校验。
3. **上下文只由有效历史构建**：屏幕上显示的全部历史不等于发给模型的历史。
4. **证据语义诚实**：检索排名只用于排序，不宣称「主要论据」。

## 分层

```text
ConversationState (per Entry)   业务数据：轮次、状态、证据、引用
RagConversationModel            Qt Item Model：结构通知 + 角色数据
RagConversationStore (接口)     持久化边界：v1 = InMemory，后续可换 QSettings/JSON
RagDocumentCoordinator          检索、生成、取消、状态迁移
```

### 持久化边界

v1 明确为「同一运行期内按文档保存对话」，切换 tab 不丢；跨重启恢复作为后续功能。
先定义接口，v1 用内存实现，索引清空 / 删除当前文档索引时同步清理。

```cpp
class RagConversationStore {
public:
    virtual ~RagConversationStore() = default;
    virtual QVector<RagConversationTurn> load(const QString& documentHash) = 0;
    virtual void save(const QString& documentHash,
                      const QVector<RagConversationTurn>& turns) = 0;
    virtual void remove(const QString& documentHash) = 0;
};
```

## 数据模型

```cpp
enum class RagTurnState { Retrieving, Generating, Ready, Failed, Cancelled };

enum class RagChatFailure {
    None, Timeout, Unreachable, ModelMissing,
    BudgetEvidence, ContextTooSmall,
    Rejected, Cancelled, Unknown
};

struct RagConversationTurn {
    QString id;                       // 逻辑轮次 ID
    quint64 generation = 0;           // 生成尝试版本，重新生成时 +1
    QString question;
    QString answerText;               // 最终文本（不含 [[cite:]]）
    QVector<RagCitation> citations;   // 统一编号列表
    QVector<RagEvidence> evidence;    // 本轮证据，重生成复用
    RagTurnState state = RagTurnState::Retrieving;
    RagChatFailure failure = RagChatFailure::None;
    QString error;
    std::optional<int> promptTokens;  // promptEvaluated 有值才填
};
```

- `turnId` 标识逻辑轮次，`generation` 标识生成尝试，两者分离。
- 历史由有效轮次派生。每个轮次带 `historyEpoch`，换 Chat 模型时递增
  `Entry::historyEpoch`；只有当前纪元的完成轮次参与上下文，屏幕上的旧轮次保留：

```cpp
QStringList historyForPrompt(const QVector<RagConversationTurn>& turns,
                             quint64 historyEpoch,
                             const QString& currentTurnId);
```

### Model 通知规范

- 新增轮次：`beginInsertRows/endInsertRows`；
- 单轮状态或文本变化：`dataChanged`（流式 delta 节流 80–100ms）；
- 截断 / 清空：`beginRemoveRows/endRemoveRows`；
- 业务信号（`turnStateChanged` 等）只作附加事件，不替代结构通知。

## 任务生命周期与并发

### 单活动任务

- 同一文档同一时刻只允许一个活动任务（检索或生成）。
- 生成期间输入框可编辑，发送按钮禁用；停止按钮只取消当前任务。
- 取消完成后才能提交下一轮。
- `busy` 由活动任务状态推导，不在多个分支手动 `true/false`。

### 回调校验

```cpp
if (documentHash != snapshot.key.contentHash) return;
if (turnId != m_activeTurnId) return;
if (generation != turn.generation) return;
```

取消只是尽快停止底层任务，正确性完全依赖三重校验。

### 重新生成事务

```text
1. 校验目标轮次属于当前文档
2. 计算需截断的后续轮次（UI 确认）
3. 使当前活动任务失效：clearActiveTask() + cancel
4. 截断后续轮次
5. turn.generation += 1；清空 answer/citations/failure；state = Generating
6. 用 turn.evidence 生成；只接受新 generation 的回调
7. Ready / Failed / Cancelled 三态收尾
```

## 上下文构建（纯函数）

```text
有效轮次（state == Ready）
    ↓ 丢弃 Retrieving / Generating / Failed / Cancelled 的半成品
按时间顺序
    ↓ 取最近 maxHistoryTurns 个完整轮次
按 token 预算裁剪
    ↓
拼接当前问题 + 本轮证据
    ↓
ChatRequest（含 droppedHistoryTurns / estimatedInputTokens）
```

- `droppedHistoryTurns` 由 `ContextAssembler::assembleBudgeted` 返回，UI 直接用。
- 提示写成「部分较早对话因预算未发送给模型」，不暗示模型记住了全部屏幕内容。
- `promptTokens` 用 `std::optional<int>`，`promptEvaluated` 有真实值才填。

## 失败类型与映射

```text
OllamaChatTask
   ↓ ProviderError（timeout / connect / http status / server error / model missing）
ProviderErrorMapper
   ↓ RagChatFailure
Coordinator 状态迁移
   ↓ UI 文案 + 可执行操作
```

- `Cancelled` 是正常终态，不用红色错误样式。
- 预算错误分为 `BudgetEvidence` 与 `ContextTooSmall`；历史按预算裁剪，不视为失败。
- `ModelMissing` / `Unreachable` 由 Provider 适配层判断，UI 不解析 HTTP 状态码。

## UI

- `QScrollArea` + 每轮 `TurnCard`；20 轮 + 总字符量双上限。
- 卡片：问题、回答、引用列表、操作行（复制 / 重新生成 / 停止 / 重试）。
- 输入框为多行 `QPlainTextEdit`：`Enter` 换行、`Ctrl+Enter` 发送，并显示快捷键提示。
- 骨架：静态占位 + 现有进度条，不引入 shimmer。
- 卡片销毁时断开该轮异步连接；不静默删除未完成轮次。

### Markdown 与引用

- 完成后 `QTextDocument::setMarkdown(..., MarkdownDialectGitHub)`；生成中纯文本。
- `QTextBrowser::setOpenLinks(false)`；只处理白名单锚点
  `readium-cite:<chunkId>`，并校验 ID 在本轮引用集合中。
- 编号只在最终 `citations` 列表统一分配，内联链接与列表共用编号。
- 复制用 `plainText`。
- P1–P3 不做「主要 / 补充依据」；后续若要分类，必须基于结论与引用的支撑关系。

### 引用与 Viewer 联动

```cpp
struct ViewerPosition { int page = 0; qreal offsetY = 0; };
ViewerPosition captureViewport() const;
void restoreViewport(const ViewerPosition&);
```

- 返回语义：记录「最近一次跳转前」的位置；连续点击多个引用时只在首次跳转前更新快照。
- 返回时清除快照与证据高亮，避免跨文档残留。

## 分期

### P1 数据模型、状态机与任务生命周期

- Turn（`id + generation`）、状态迁移、单活动任务、`RagConversationStore` 边界（v1 内存）。
- `askQuestion / regenerateTurn / stopActiveTurn / clearConversation`。
- 回调三重校验、`busy` 推导、历史纯函数。
- 测试：迟到回调不覆盖、多次重生成、文档切换不串数据、失败轮不进历史、
  `droppedHistoryTurns` 来自 assembler。

### P2 对话 UI 与基础操作

- TurnCard、追问、复制、停止、重试、清空、加载 / 失败状态。
- 单活动任务下的按钮禁用策略；输入框快捷键。

### P3 Markdown 与引用呈现

- Markdown、内联引用锚点白名单、统一编号、引用列表、长回答折叠。

### P4 Viewer 联动

- `captureViewport/restoreViewport`、引用跳转、高亮、返回原位置。

### P5 错误体验、持久化与收尾

- 三类预算错误、超时 / 不可达 / 模型缺失文案；持久化接入；边界测试与文档。

## 明确不做

- 虚拟列表、shimmer 动画、独立聊天 UI 框架。
- 多轮并行生成；独立 taskId（`turnId + generation` 已足够）。
- 把检索分数当作「主要论据」。
- 让 UI 解析 Provider 原始错误。
