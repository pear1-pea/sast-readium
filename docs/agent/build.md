# Build

CMake 3.28+ is the primary build system; xmake is experimental.

## macOS release flow (system Clang + Ninja)

```bash
cmake --fresh --preset macOS-Release-user   # from CMakeUserPresets.json
cmake --build build --config Release
ctest --test-dir build --output-on-failure
pre-commit run --all-files
./build/app/app
```

Keep Homebrew-specific paths in `CMakeUserPresets.json` (git-ignored), not in the committed
presets in `CMakePresets.json`.

## Presets

| Platform | Presets |
| --- | --- |
| macOS | `macOS-Debug`, `macOS-Release`, `macOS-RelWithDebInfo` |
| Linux/Unix | `Debug-Unix`, `Release-Unix` |
| Windows (MSYS2) | `Debug-MSYS2`, `Release-MSYS2` |
| Windows (vcpkg / MSVC) | `Debug-Windows`, `Release-Windows`, `Debug-Windows-Ninja`, `Release-Windows-Ninja` |
| Cross / ARM64 | `Release-Linux-ARM64`, `Release-Windows-ARM64` |
| vcpkg variants | append `-vcpkg` to the Unix/MSYS2 presets |

## SQLite requirements

Ask PDF uses the Qt SQL SQLite driver (system SQLite on macOS/Linux package
builds). The SQLite build must include FTS5 (available since SQLite 3.9); an
index cannot be created otherwise. The implementation avoids SQLite UPSERT
syntax, so SQLite 3.24 is not required.

## Environment notes

- MSYS2: prefer system packages; set `FORCE_VCPKG=ON` to force vcpkg.
- vcpkg: set `-DUSE_VCPKG=ON`.
- Linux: use the generic CMake presets with system packages.
- This project enables only `CXX`; do not add `CMAKE_C_COMPILER` unless C sources are added.

## Build rules

- Do not mix generators inside one `build/` tree; reconfigure with `--fresh` after a
  generator switch or a cache-affecting preset change.
- Use the pkg-config path from the user preset for macOS package discovery; do not export
  ad hoc shell paths in docs or scripts.

## Related docs

- [Build Systems](../build-systems/index.md) — CMake vs xmake and per-system guides
- [Dependency Management](../getting-started/dependency-management.md) — tiered strategy
- [MSYS2 Build Setup](../setup/msys2-build.md) — Windows MSYS2
- [clangd IDE Setup](../setup/clangd-setup.md) — language server / IDE integration
