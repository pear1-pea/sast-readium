# AGENTS.md

SAST Readium — Qt6 PDF reader, C++20. This is the single entry point for coding
agents (Claude Code / Codex / OpenCode / Kilo Code). Keep this file short and put
details in `docs/`; load them on demand.

`CLAUDE.md` is a symlink to this file.

## Project

- Stack: Qt6 6.8.2, poppler-qt6, spdlog
- Build: CMake 3.28+ (primary) / xmake (experimental)
- Platforms: macOS, Linux, Windows (MSYS2 / vcpkg)
- Layout: `app/` runtime · `tests/` verification · `docs/` docs · `scripts/` helpers
  - `app/rag/` is the Ask PDF subsystem (extract / index / retrieve / embedding / chat / ui)

## Commands

```bash
cmake --fresh --preset macOS-Release-user
cmake --build build --config Release
ctest --test-dir build --output-on-failure
pre-commit run --all-files
./build/app/app
```

## Hard rules

- Always run build + `ctest` + `pre-commit run --all-files` before landing changes.
- When changing `app/` or `tests/`, add/update tests in the same change.
- Never hardcode personal paths (e.g. `/opt/homebrew`) in `CMakePresets.json`;
  machine overrides go in `CMakeUserPresets.json` (git-ignored).
- Never mix generators in one `build/` tree; reconfigure with `--fresh`.
- Keep logic inside existing boundaries: `model/ controller/ view/ ui/ cache/
  managers/ utils/ rag/`.
- Style is enforced by `.clang-format` + pre-commit; do not hand-negotiate it.

## Docs map — read only what you need

| Need | Read |
| --- | --- |
| Full documentation index | `docs/index.md` |
| Architecture and boundaries | `docs/agent/architecture.md` |
| Code / logging conventions | `docs/agent/conventions.md` |
| Build presets and matrix | `docs/agent/build.md` |
| Test layout and commands | `docs/agent/testing.md` |
| Platform build guides | `docs/getting-started/`, `docs/build-systems/` |
| Dependencies (tiers) | `docs/getting-started/dependency-management.md` |
| macOS/Linux toolchain, clangd | `docs/setup/clangd-setup.md` |
| Windows MSYS2 | `docs/setup/msys2-build.md` |
| Debugging | `docs/debugging-setup.md` |
| Logging (spdlog) | `docs/logging-system.md` |
| Feature internals | `docs/features/` |
