# SAST Readium — Project CLAUDE.md

Overrides and supplements the user-level CLAUDE.md. Inherits all user-level rules (v19) — only project-specific additions below.

## 1. Project Identity

- **Name**: sast-readium — Qt6 PDF reader
- **Language**: C++20
- **Build**: CMake ≥3.28, Qt6 6.8.2, spdlog, poppler-qt6
- **Platforms**: macOS (primary), Linux, Windows (MSYS2 / vcpkg)
- **CI**: GitHub Actions (`.github/workflows/`)

## 2. Build system

### macOS (Homebrew)

Generic branch in CMakeLists.txt lines 91-101 — this is the macOS path:

```cmake
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg LinguistTools Concurrent TextToSpeech)
find_package(spdlog REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(POPPLER_QT6 REQUIRED poppler-qt6)
pkg_get_variable(_poppler_inc_dir poppler-qt6 includedir)
pkg_get_variable(_poppler_libdir poppler-qt6 libdir)
add_library(PkgConfig::POPPLER_QT6 INTERFACE IMPORTED)
target_include_directories(PkgConfig::POPPLER_QT6 SYSTEM INTERFACE "${_poppler_inc_dir}")
target_link_libraries(PkgConfig::POPPLER_QT6 INTERFACE "${_poppler_libdir}/libpoppler-qt6.dylib")
```

Build command:
```bash
PKG_CONFIG_PATH="/opt/homebrew/opt/poppler-qt6/lib/pkgconfig:$PKG_CONFIG_PATH" \
  CC=/usr/bin/clang CXX=/usr/bin/clang++ \
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Use system Clang (`/usr/bin/clang++`), **not** Homebrew LLVM — LLVM has compatibility issues with this project's dependencies.

### MSYS2 (Windows)

CMakeLists.txt detects `MSYSTEM` env var automatically. Use MSYS2 system packages, not vcpkg (unless `FORCE_VCPKG=ON`).

### vcpkg

Set `-DUSE_VCPKG=ON` to force vcpkg mode. Packages: Qt6 (Core/Gui/Widgets/Svg/LinguistTools/Concurrent/TextToSpeech), spdlog, poppler-qt6.

## 3. Code architecture

```
app/
├── main.cpp              ← Entry point
├── MainWindow.(h|cpp)    ← Main window
├── managers/             ← Core managers (PDF, cache, tab, etc.)
├── controller/           ← Controllers (Document, Page)
├── view/                 ← Views (PDF, thumbnails, etc.)
├── model/                ← Data models
├── delegate/             ← Custom delegates
├── ui/                   ← UI components
├── utils/                ← Utilities
├── cache/                ← Rendering cache
├── plugin/               ← Plugin system
├── command/              ← Command pattern
├── factory/              ← Factory classes
├── i18n/                 ← Translations (app_zh.ts)
└── config.h.in           ← Config template
```

### Core managers (managers/)
- `PDFManager` — PDF document loading and rendering (wraps poppler)
- various others

## 4. Code conventions

- **Naming**: Follow existing Qt conventions (`m_` member prefix, PascalCase classes, camelCase methods)
- **Includes**: Use `<poppler/qt6/poppler-qt6.h>` style (NOT `<poppler-qt6.h>`) — see memory note about Homebrew include path
- **Qt**: AUTOMOC/AUTOUIC/AUTORCC enabled globally — `.moc` includes are not needed
- **Logging**: spdlog-based, config at `config/logging.json` / `config/logging-dev.json`
- **clangd**: Auto-configured by `scripts/update-clangd-config.sh`. If clangd can't find headers, check `.clangd` was updated after cmake configure

## 5. Testing

Tests in `tests/`:
- `unit/` — Unit tests
- `integration/` — Integration tests
- `performance/` — Performance benchmarks
- `smoke_test.cpp` — Quick smoke test
- `test_qgraphics_pdf.cpp` — QGraphics PDF renderer tests

Run: `ctest --test-dir build` or `./build/tests/<test_name>`

## 6. Pre-commit hooks

Configured in `.pre-commit-config.yaml`:
- trailing-whitespace, end-of-file-fixer, check-added-large-files, check-merge-conflict
- YAML/JSON/XML validation
- `no-commit-to-branch` (protects main/master)
- clang-format (`.cpp`/`.hpp`/`.c`/`.h`) — **run before every commit**
- cmake-format (`CMakeLists.txt`/`.cmake`)
- black (`.py`)

Run: `pre-commit run --all-files`

## 7. Git conventions

- **Branch naming**: `<type>/<short-kebab>` — matching existing branches (`refactor/log`, `fix/poppler-include`)
- **Conventional Commits**: Inherit from user-level §6
- **Current refactor**: `refactor/log` branch — extracting logging and cache from PDFViewerEnhancements singleton. Any changes touching `PDFViewerEnhancements` or related logging/cache classes should coordinate with this work

## 8. Poppler include path (critical)

Homebrew's poppler-qt6 uses `includedir=/opt/homebrew/include` (NOT `.../include/poppler/qt6/`).
- All files must use `#include <poppler/qt6/poppler-qt6.h>`
- This is already handled by the CMake Generic branch's `target_include_directories` with SYSTEM INTERFACE pointing to the raw includedir

## 9. Anti-patterns

- ❌ Don't introduce new dependencies without evaluation (standard lib > existing deps > new)
- ❌ Don't mix include styles (`poppler-qt6.h` vs `poppler/qt6/poppler-qt6.h`)
- ❌ Don't override CMAKE_AUTOMOC/AUTOUIC/AUTORCC — they're set globally
- ❌ Don't commit to `main`/`master` — pre-commit hook enforces this
- ❌ Don't bypass the Generic branch in CMakeLists.txt for macOS builds
