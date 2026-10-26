# Ask PDF

Ask PDF adds document evidence search, optional local semantic retrieval, and
optional local answer generation to the reader. Lexical search works without any
external service.

## Data flow

```text
PDF -> text extraction -> chunks -> SQLite FTS5 index
                                  -> optional Ollama embeddings
query -> lexical + optional vector retrieval -> evidence list
      -> optional Ollama chat -> validated citations -> page highlight
```

The PDF text is extracted on a worker thread. The renderer and the indexer use
separate Poppler documents so indexing and page rendering do not share mutable
Poppler state.

## Local services

Both integrations are local-only and disabled by default. Configure them with
the gear button in the Ask PDF sidebar:

| Service | Endpoint | Configuration |
| --- | --- | --- |
| Embeddings | `<endpoint>/api/embed` | Embedding model (hidden version) |
| Chat | `<endpoint>/api/chat` | Chat model + "AI 回答" toggle |

The endpoint defaults to `http://127.0.0.1:11434` and must point at the loopback
interface (`127.0.0.1`, `localhost`, or `::1`); remote hosts, user-info, query
strings, and fragments are rejected so PDF text never leaves the machine.
Settings are stored in the application settings under `rag/ollama/*`; the
environment variables
`READIUM_OLLAMA_EMBED_MODEL`, `READIUM_OLLAMA_EMBED_VERSION` and
`READIUM_OLLAMA_CHAT_MODEL` are only used to seed the values on first launch.
The migrated result is written back, including empty values, so later
environment changes cannot silently override the dialog.

"Test connection" checks the service and model list first, then performs the
embedding and chat checks one after another so two cold models are not loaded at
the same time. A successful test only means the service and model are usable at
that moment; it does not mean the current document has vectors. Requests are
asynchronous, cancellable, and their results are ignored after the address or
model changes. The model dropdown lists every model returned by `/api/tags`
without filtering by capability; choosing the wrong model is reported by the
per-capability check, and filtering with `/api/show` is left for a later
iteration.

Readium never starts or downloads an Ollama model. If embeddings are unavailable,
the lexical path remains usable. If answers are disabled, Ask PDF still returns
evidence and page navigation, and semantic retrieval keeps working when vectors
are ready. Changing the chat model keeps the conversation on screen but starts a new
history boundary: later answers are generated without earlier turns as model
context. Toggling answers off and on does not reset history.

## Progress and index management

While a document is indexed the panel shows a progress bar with the current
phase:

| Phase | Progress |
| --- | --- |
| Checking cache | indeterminate |
| Extracting text | 0–60%, `提取 N/M 页` |
| Chunking | 60–70%, `切分文本 N/M 页` |
| Writing the FTS index | indeterminate at 70% |
| Generating vectors | 80–100%, `向量 N/M` (only when embeddings are configured) |

Worker threads report progress through `RagJobContext::onProgress`; both the
worker and the coordinator throttle updates to roughly one publish per 100 ms
and always publish the final value. Vector progress counts the vectors that
exist for the configured model (including ones stored by an earlier run), so
`向量 N/M` reaches `M` when the document is fully embedded. Progress is reset
when the document is rebuilt, cancelled or its index is deleted.

The gear next to the status row opens the local AI settings; the index button
opens **Ask PDF 索引管理**:

- Current document: file name, index state (including the typed failure
  reason), page/chunk/vector counts, and the logical size (text + vector
  bytes). Actions: rebuild, delete the current document index.
- All documents: document count and the SQLite file size. Action: clear every
  index.

Deleting the current document index removes only the path mapping for this
document. Content shared with another open tab is kept until its last path is
removed. Deleting or clearing an index never modifies the PDF file. The
per-document size is a logical size and can differ from the file size shown for
the whole database.

## Conversation UI

Each question becomes a turn card: question, answer, citations and per-turn
actions (copy, regenerate, stop, retry). Answers are rendered as Markdown after
generation completes; while streaming the card shows plain text. Long answers
start collapsed and can be expanded. Citation numbers are assigned once by the
validator and shared by inline links and the citation list (`[1] 第 3 页 · 方法`).

Typed chat failures drive the copy and actions:

- timeout: "回答生成超时，模型可能正在加载。" (retry)
- unreachable: "无法连接本地 Ollama，请确认服务已启动。" (retry / settings)
- missing model: "Chat 模型不存在，请先在 Ollama 中安装。" (settings)
- evidence/history budget: retry, or clear the conversation when history no
  longer fits the context window
- cancelled: a normal terminal state, not a red error

Clicking a citation records the viewer position before jumping. "返回原位置"
restores the page and continuous-scroll offset and clears the evidence
highlight. Only the first jump of a series updates the saved position.

## Conversation persistence

Completed turns (question, answer, numbered citations) are stored in
`<QStandardPaths::AppDataLocation>/rag/conversations.json` with owner-only
permissions. In-flight, failed and cancelled turns are not persisted; evidence
is not stored, so regenerating a restored turn runs retrieval again. Clearing
the conversation, rebuilding or deleting the index removes the stored turns.

## Empty and error states

Ask PDF shows an inline state block above the answer/evidence area. The state is
derived from the index state, the failure reason, whether a search has run, and
whether answer generation is enabled:

| Situation | Message | Actions |
| --- | --- | --- |
| No document | 打开 PDF 后即可搜索证据。 | – |
| Answer generation disabled | 未启用回答生成。当前仅显示本地证据。 | 配置本地模型 |
| No relevant evidence | 没有找到足够相关的证据。尝试换一种问法或重建索引。 | 换个问法 / 重建索引 |
| Semantic retrieval unavailable | 语义检索不可用，当前使用关键词检索。 | 检查本地模型 |
| Encrypted PDF | PDF 已加密，无法提取文本。 | 重试 |
| Corrupt PDF | PDF 无法解析，文件可能已损坏。 | 重试 |
| Scanned/image-only PDF | 当前 PDF 没有可提取文本，可能是扫描版。 | – |
| Index database error | 本地索引不可用或已损坏。 | 重建索引 / 清除索引 |
| Retrieval failed | 检索失败。 | 重试 |
| Answer generation failed | 回答生成失败。 | 重试 / 检查本地模型 |

Failure reasons are typed (`RagIndexFailureReason`), not matched from error
strings. When a search returns no evidence, answer generation is skipped so the
empty state is not mixed with a chat error. "Retry" for answers reuses the last
evidence without running a new search; use "换个问法" to search again.

## Vector identity and model changes

Vectors are keyed by `(document_hash, chunk_id, chunk_hash, model_id,
model_version)`. Changing the embedding model therefore starts from the target
model's namespace: only its missing vectors are generated, and switching back to
a model with complete vectors reuses them without recomputing. Text is not
re-extracted.

- A model chosen in the UI stores the hidden version `1`. A version migrated
  from `READIUM_OLLAMA_EMBED_VERSION` is preserved, and a migration without a
  version keeps embedding disabled (the old behaviour).
- The endpoint is intentionally not part of the vector identity. The same model
  name and version served from another endpoint reuses existing vectors.
- Changing the weight behind an unchanged Ollama tag (`latest` re-pulled) is not
  detected. Retrieval quality may drift until the tag or endpoint changes; this
  is a known limitation, not a bug.

## Persistence and privacy

The index is stored at:

```text
<QStandardPaths::AppDataLocation>/rag/rag-index.sqlite
```

It contains the extracted PDF text, chunk metadata, and optional embedding
vectors. The `rag` directory is created owner-only (`0700`) and a newly created
database file is set to owner read/write (`0600`) on platforms that support POSIX
permissions. On Windows these calls only affect the read-only bit; access control
comes from the user profile ACL.

The index persists after a tab is closed. A document is not automatically removed
from the index when its tab closes, because keeping the cache avoids rebuilding
after reopening the same file. Use **Ask PDF → clear index** to delete all local
index data. The clear operation uses SQLite `secure_delete` before deleting rows
and runs `VACUUM` afterwards to reclaim file space. Clearing the index does not
modify the PDF files themselves.

SQLite must be built with FTS5 enabled (SQLite 3.9+); see the build
documentation for the supported baseline.

## Extraction status

- Text PDFs are indexed normally.
- Scanned/image-only PDFs are reported as unsupported until OCR is added.
- Encrypted or unreadable PDFs enter the failed state but keep their file
  identity so the user can retry after fixing the file.

## Planned improvements

See the [chunking and evidence completeness TODO](ask-pdf-todo.md) for priorities,
dependencies, and acceptance criteria.

The context assembler sends whole evidence blocks only. Blocks that do not fit
the remaining budget are skipped; smaller later blocks can still be included.
Citation metadata therefore refers to the full text actually assembled. Evidence
is selected first, then recent complete history turns are added only if they
fit, and the output reservation is subtracted before either.

Before assembly, retrieval expands each hit with its immediate reading-order
neighbours (same or one adjacent page, bounded text size) and merges overlapping
or touching source ranges. The merged block keeps the highest-ranked chunk id,
so citations and page highlights still resolve, and the duplicated overlap text
is sent once.

The budget covers the system prompt, question, history, evidence and output
reservation. It is estimated from UTF-8 bytes plus per-message overhead: a
conservative upper bound rather than a model tokenizer, so a request that fits
the estimate also fits the declared window. The request sets `num_ctx` to that
window and `num_predict` to the output reservation, so the service limit no
longer depends on an Ollama version default. Each answer reports
`prompt_eval_count`, which is logged against the estimate; it is the real
tokenizer count and the signal for tightening the bound.

Each indexed document records its chunking strategy
(`target/max/overlap/sentence-aware`). Reopening a document whose stored policy
differs rebuilds its chunks, FTS rows and vectors instead of mixing strategies.
Page section paths come from the PDF outline when one exists; they are used for
reranking only, never sent as evidence.

New embedding requests set `truncate: false`. Service errors retain the HTTP
status and Ollama diagnostic; failed embedding jobs leave lexical search usable.
Previously cached vectors are unchanged. Rebuild the document index to regenerate
them with truncation disabled.

See the [initial evaluation record](ask-pdf-evaluation.md) for the synthetic
evidence benchmark, its reproduction command, and remaining limitations.
