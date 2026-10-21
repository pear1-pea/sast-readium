# Conventions

Code style is enforced by `.clang-format` plus pre-commit; do not hand-negotiate formatting.

## C++ / Qt

- Follow existing Qt naming: `m_` members, PascalCase classes, camelCase methods.
- Include Poppler as `#include <poppler/qt6/poppler-qt6.h>`.
- Keep AUTOMOC/AUTOUIC/AUTORCC assumptions intact; register new Qt classes with the
  appropriate CMake target.
- Keep new code C++20 and inside the existing `app/` boundaries (see
  [Architecture](architecture.md)).
- Write comments in English, and only where they add value.

## Logging

- Logging is spdlog-based; use the project macros (`LOG_INFO`, `LOG_WARNING`, `LOG_ERROR`,
  `LOG_DEBUG`) from `utils/LoggingMacros.h`.
- Do not introduce `qDebug()` or `std::cout` for application logging.
- See [Logging System](../logging-system.md) for sinks, levels, and runtime configuration.

## Anti-patterns

- Hardcoding personal paths (e.g. `/opt/homebrew`) in committed presets.
- Reintroducing Makefiles into a `build/` tree that has been configured for Ninja.
- Adding new dependencies without evaluating the existing stack first.
- Changing build behavior without verifying configure, build, and tests.
