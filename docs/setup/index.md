# Setup & Configuration

This section provides comprehensive guides for setting up your development environment and configuring tools for optimal SAST Readium development.

## Overview

Proper development environment setup is crucial for a smooth development experience. This section covers platform-specific build setup and IDE configuration to ensure you have all the tools needed for effective development.

## Documentation in this Section

### [MSYS2 Build Setup](msys2-build.md)

Comprehensive guide for setting up SAST Readium development on Windows using MSYS2:

- MSYS2 installation and configuration
- Dependency installation via pacman
- Build system configuration
- CMake presets for MSYS2
- Troubleshooting common issues
- Performance optimization tips

### [clangd IDE Setup](clangd-setup.md)

Cross-platform support for clangd configuration management:

- Automatic clangd configuration for enhanced IDE features
- Platform-specific setup scripts (Windows, Linux, macOS)
- Build directory detection and configuration
- IDE integration (VS Code, CLion, etc.)
- Troubleshooting clangd issues

## Platform-Specific Recommendations

### Windows

- **Recommended**: Use MSYS2 for the best development experience
- **Alternative**: Native Windows with vcpkg (more complex setup)
- **IDE**: VS Code with clangd extension

### Linux

- **Package Manager**: Use system packages (apt, dnf, etc.)
- **Build System**: CMake with system dependencies
- **IDE**: Any editor with clangd support

### macOS

- **Package Manager**: Homebrew for dependencies
- **Build System**: CMake with Homebrew packages
- **IDE**: Xcode or VS Code with clangd

## Quick Setup Guide

1. **Choose your platform approach**:

   - Windows: Follow [MSYS2 Build Setup](msys2-build.md)
   - Linux/macOS: Install system dependencies, then configure clangd

2. **Configure IDE integration**:

   - Follow [clangd IDE Setup](clangd-setup.md) for enhanced development experience

3. **Verify setup**:
   - Build the project using your chosen build system
   - Ensure clangd is working for code completion and error checking

## Next Steps

After completing the setup:

- Explore the [Features](../features/) documentation to understand the codebase
- Review the [Build Systems](../build-systems/) documentation for build customization
- Start developing with full IDE support and optimized build environment
