# SAST Readium — Project CLAUDE.md

This file supplements the user-level CLAUDE.md. Keep project-specific rules here, and keep them aligned with the current build and code layout.

## 1. Project Identity

- Name: `sast-readium`
- Purpose: Qt6 PDF reader
- Language: C++20
- Build: CMake 3.28+, Qt6 6.8.2, spdlog, poppler-qt6
- Platforms: macOS, Linux, Windows (MSYS2 / vcpkg)
- CI: GitHub Actions

## 2. Environment & Commands

### macOS release flow

- Use the system Clang toolchain: `/usr/bin/clang++`
- Keep Homebrew-specific paths in `CMakeUserPresets.json`, not in committed presets
- Use Ninja on macOS
- Reconfigure with `--fresh` after a generator switch or preset change that touches the cache

```bash
cmake --fresh --preset macOS-Release-user
cmake --build build --config Release
ctest --test-dir build --output-on-failure
pre-commit run --all-files
./build/app/app
```

### Other supported flows

- MSYS2: use system packages unless `FORCE_VCPKG=ON`
- vcpkg: set `-DUSE_VCPKG=ON`
- Linux/system packages: use the generic CMake presets already in the repo

## 3. Build Rules

- Keep `CMakePresets.json` portable. Do not hardcode personal Homebrew prefixes there.
- Put machine-specific macOS overrides in `CMakeUserPresets.json`, and ignore that file in git.
- Do not mix generators inside one `build/` tree. If `build/` was configured by a different generator before, refresh it before rebuilding.
- This project only enables `CXX`; do not add `CMAKE_C_COMPILER` for this codebase unless the project starts compiling C sources.
- For macOS package discovery, use the pkg-config path from the user preset, not ad hoc shell exports in docs or scripts.

## 4. Repo-Wide Rule

- When changing code under `app/` or `tests/`, update or add tests for the behavior change in the same change when practical.
- Do not land source changes without reviewing the resulting diff and running the appropriate validation.

## 5. Architecture

- `app/` contains runtime code.
- `tests/` contains verification code.
- Keep logic inside the existing boundaries: `model/`, `controller/`, `view/`, `ui/`, `cache/`, `managers/`, `utils/`.
- For cross-cutting behavior, prefer extending the existing owner of that responsibility instead of adding a parallel path.

## 6. Where Things Live

- `app/ui/viewer/` — viewer surface and rendering-related UI
- `app/ui/continuous/` — continuous-view layout, blueprint, render scheduling
- `app/model/` — document state and derived models
- `app/managers/` — shared app services
- `tests/unit/` — unit tests
- `tests/integration/` — integration tests
- `docs/` — setup, build, and troubleshooting notes

## 7. Code Conventions

- Follow existing Qt naming: `m_` members, PascalCase classes, camelCase methods
- Use English comments only when needed
- Include Poppler as `#include <poppler/qt6/poppler-qt6.h>`
- Keep AUTOMOC/AUTOUIC/AUTORCC assumptions intact
- Keep logging spdlog-based

## 8. Anti-Patterns

- Hardcoding `/opt/homebrew` or other personal paths in committed presets
- Reintroducing Makefiles into the macOS `build/` tree after it has been configured for Ninja
- Adding new dependencies without evaluating the existing stack first
- Changing build behavior without verifying configure, build, and tests
