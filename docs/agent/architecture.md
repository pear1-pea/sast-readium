# Architecture

SAST Readium is a Qt6 PDF reader. Runtime code lives in `app/`; verification lives in `tests/`.

## Top-level layout

| Path | Purpose |
| --- | --- |
| `app/` | Runtime code |
| `tests/` | Unit and integration tests |
| `docs/` | Documentation (start at `index.md`) |
| `scripts/` | Developer and build helper scripts |
| `ui/` | Original web/design mockups (not compiled into the app) |
| `assets/`, `config/`, `distrib/` | Resources, configuration, packaging |
| `cmake/` | CMake modules |
| `build/`, `build-asan/` | Generated build trees (never committed) |

## Runtime boundaries (`app/`)

Keep logic inside the existing owner of a responsibility instead of adding a parallel path.

| Directory | Responsibility |
| --- | --- |
| `app/core/` | Application bootstrap and component wiring |
| `app/common/` | Small shared types (e.g. `Theme.h`) |
| `app/model/` | Document state and derived models |
| `app/controller/` | Controllers coordinating model and view |
| `app/view/` | View base types |
| `app/ui/` | Widgets and rendering UI (`viewer/`, `continuous/`, `thumbnail/`, `page/`, `dialogs/`, `widgets/`) |
| `app/delegate/` | Item delegates for views |
| `app/cache/` | Caching layers |
| `app/managers/` | Shared application services |
| `app/utils/` | Utilities, including logging |
| `app/plugin/` | Plugin manager |
| `app/i18n/` | Translations |
| `app/rag/` | Ask PDF subsystem (see below) |

## Ask PDF subsystem (`app/rag/`)

Hybrid lexical (FTS5) plus optional local embedding retrieval, with an optional local LLM chat.

| Directory | Responsibility |
| --- | --- |
| `app/rag/domain/` | Domain types |
| `app/rag/extract/` | Text extraction from PDFs |
| `app/rag/index/` | FTS5 / lexical index |
| `app/rag/embedding/` | Optional Ollama embeddings and vector cache |
| `app/rag/retrieve/` | Retrieval and ranking |
| `app/rag/chat/` | Local LLM chat session |
| `app/rag/controller/` | Coordination with the document lifecycle |
| `app/rag/ui/` | Ask PDF sidebar widgets |

External services are local-only: embeddings at `127.0.0.1:11434/api/embed`, chat at
`127.0.0.1:11434/api/chat`. The app never starts or downloads a model. See the
[README](../../README.md) for the `READIUM_OLLAMA_*` environment variables.

## Cross-cutting rules

- For cross-cutting behavior, extend the existing owner of that responsibility instead of
  adding a parallel path.
- Keep code inside the boundaries above; add a new top-level `app/` directory only when a
  truly new responsibility appears, and document it here.
