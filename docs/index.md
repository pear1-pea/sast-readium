# SAST Readium Documentation

Index of all project documentation. The agent entry point is
[AGENTS.md](../AGENTS.md).

## Getting Started

- [Overview](getting-started/index.md)
- [Dependency Management](getting-started/dependency-management.md) — tiered system-packages vs vcpkg strategy
- [Platform Support](getting-started/platform-support.md) — supported platforms and architectures

## Build Systems

- [Overview](build-systems/index.md)
- [CMake vs Xmake](build-systems/build-system-comparison.md) — feature comparison
- [Xmake](build-systems/xmake/index.md)
  - [Build Guide](build-systems/xmake/xmake-build.md)
  - [Status Report](build-systems/xmake/xmake-status.md)
  - [Final Report](build-systems/xmake/xmake-final-report.md)

## Setup & Configuration

- [Overview](setup/index.md)
- [MSYS2 Build Setup](setup/msys2-build.md) — Windows MSYS2 environment
- [clangd IDE Setup](setup/clangd-setup.md) — language server / IDE integration

## Features

- [Overview](features/index.md)
- [Ask PDF](features/ask-pdf.md) — local evidence search, optional embeddings and chat
- [Thumbnail System](features/thumbnail-system.md) — Chrome-style PDF thumbnails

## Development (agent-focused)

- [Architecture](agent/architecture.md) — top-level layout and `app/` boundaries
- [Conventions](agent/conventions.md) — C++/Qt style, logging, anti-patterns
- [Build](agent/build.md) — presets and per-platform build matrix
- [Testing](agent/testing.md) — test layout and commands
- [Debugging Setup](debugging-setup.md) — VS Code, per-platform debuggers
- [clangd Configuration](clangd-configuration.md) — Qt "missing-includes" fixes
- [Logging System](logging-system.md) — spdlog integration and runtime configuration

## Quick Start

See the main [README](../README.md) and [Getting Started](getting-started/index.md).
