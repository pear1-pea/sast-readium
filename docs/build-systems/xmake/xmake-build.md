# Xmake Build Guide

This document provides comprehensive instructions for building SAST Readium using the xmake build system.

## Prerequisites

### Install Xmake

**Windows:**

```powershell
# Using PowerShell
Invoke-Expression (Invoke-WebRequest 'https://xmake.io/psget.text' -UseBasicParsing).Content

# Or using Scoop
scoop install xmake
```

**Linux/macOS:**

```bash
# Using curl
curl -fsSL https://xmake.io/shget.text | bash

# Or using Homebrew (macOS)
brew install xmake
```

### Dependencies

The same dependencies as the CMake build are required:

- **Qt6** (Core, Gui, Widgets, Svg, LinguistTools, Concurrent, TextToSpeech)
- **Poppler-Qt6** for PDF rendering
- **pkg-config** for system package detection

## Quick Start

### Basic Build

```bash
# Configure and build (Debug mode)
xmake

# Configure and build (Release mode)
xmake f -m release
xmake
```

### Run the Application

```bash
xmake run
```

## Build Configuration Options

### Build Modes

```bash
# Debug build (default)
xmake f -m debug
xmake

# Release build
xmake f -m release
xmake
```

### Dependency Management

```bash
# Use system packages (default, recommended for MSYS2/Linux)
xmake f --use_vcpkg=false
xmake

# Force vcpkg usage
xmake f --force_vcpkg=true
xmake

# Use vcpkg (Windows)
xmake f --use_vcpkg=true
xmake
```

### Platform-Specific Builds

**Windows (MSYS2):**

```bash
# MSYS2 environment automatically detected
xmake f -m release
xmake
```

**Windows (vcpkg):**

```bash
# Set VCPKG_ROOT environment variable first
xmake f --use_vcpkg=true -m release
xmake
```

**Linux:**

```bash
# Install system dependencies first
sudo apt install qt6-base-dev qt6-svg-dev qt6-tools-dev libpoppler-qt6-dev

# Build
xmake f -m release
xmake
```

**macOS:**

```bash
# Install dependencies
brew install qt@6 poppler-qt6

# Build
xmake f -m release
xmake
```

## Advanced Configuration

### Custom Qt Installation

```bash
# Specify Qt directory
xmake f --qt_dir=/path/to/qt6
xmake
```

### Additional Options

```bash
# Disable clangd configuration generation
xmake f --enable_clangd=false

# Enable examples
xmake f --enable_examples=true

# Enable tests
xmake f --enable_tests=true
```

## Build Targets

```bash
# Build main application
xmake build sast-readium

# Clean build
xmake clean

# Rebuild
xmake rebuild
```

## Installation

```bash
# Install to system
xmake install

# Install to custom directory
xmake install -o /custom/install/path
```

## Known Issues and Workarounds

### Current Status

The xmake build system is **partially functional** with the following known issues:

1. **Qt Detection Assertion Error**: There's an assertion failure in Qt detection when using MSYS2 environment with MSVC compiler
2. **Qt Rules Compatibility**: Some Qt rules may not work correctly in mixed environments

### Workarounds

**For MSYS2 Users:**

```bash
# Use CMake build system instead for now
cmake --preset=Debug-MSYS2
cmake --build --preset=Debug-MSYS2
```

**For Pure Windows (non-MSYS2):**

```bash
# May work better with native Windows Qt installation
xmake f --qt_dir="C:\Qt\6.x.x\msvc2022_64"
xmake
```

## Troubleshooting

### Qt6 Not Found

```bash
# Check Qt installation
xmake show -l qt6

# Specify Qt path manually
xmake f --qt_dir=/path/to/qt6
```

### Poppler-Qt6 Not Found

Ensure poppler-qt6 development packages are installed:

**Ubuntu/Debian:**

```bash
sudo apt install libpoppler-qt6-dev
```

**MSYS2:**

```bash
pacman -S mingw-w64-x86_64-poppler-qt6
```

**macOS:**

```bash
brew install poppler-qt6
```

### Build Errors

```bash
# Verbose build output
xmake -v

# Show detailed configuration
xmake show
```

## Comparison with CMake

| Feature            | CMake                  | Xmake                   |
| ------------------ | ---------------------- | ----------------------- |
| Configuration      | Complex CMakeLists.txt | Simple xmake.lua        |
| Package Management | Manual/vcpkg           | Built-in + vcpkg        |
| Cross-platform     | Good                   | Excellent               |
| Build Speed        | Good                   | Faster (built-in cache) |
| Learning Curve     | Steep                  | Gentle                  |

## Integration with IDEs

### VS Code

Install the xmake extension for VS Code for better integration.

### CLion

Xmake can generate compile_commands.json for CLion:

```bash
xmake project -k compile_commands
```

## Performance Tips

1. **Use Release Mode**: Always use release mode for production builds
2. **Parallel Builds**: Xmake automatically uses parallel builds
3. **Build Cache**: Xmake has built-in build cache for faster rebuilds
4. **Incremental Builds**: Only modified files are rebuilt

## Migration from CMake

Both build systems can coexist. The xmake configuration mirrors the CMake setup:

- Same source file organization
- Same dependency requirements
- Same build outputs
- Same platform support

You can switch between build systems without any source code changes.
