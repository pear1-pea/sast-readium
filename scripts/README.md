# Scripts Directory

This directory contains utility scripts for the sast-readium project.

## clangd Configuration Scripts

Cross-platform scripts to automatically update the `.clangd` configuration file to point to the correct build directory.

### Windows

#### update-clangd-config.ps1 (PowerShell - Recommended)

**Usage:**

```powershell
# Auto-detect and use the best available build directory
.\scripts\update-clangd-config.ps1 -Auto

# Use a specific build directory
.\scripts\update-clangd-config.ps1 -BuildDir "build/Release-MSYS2"

# List all available build directories
.\scripts\update-clangd-config.ps1 -List

# Enable verbose output
.\scripts\update-clangd-config.ps1 -Auto -Verbose
```

#### update-clangd-config.bat (Batch - Alternative)

**Usage:**

```cmd
REM Auto-detect and use the best available build directory
scripts\update-clangd-config.bat --auto

REM Use a specific build directory
scripts\update-clangd-config.bat build/Release-MSYS2

REM List all available build directories
scripts\update-clangd-config.bat --list
```

### Linux / macOS

#### update-clangd-config.sh (Shell Script)

**Usage:**

```bash
# Auto-detect and use the best available build directory
./scripts/update-clangd-config.sh --auto

# Use a specific build directory
./scripts/update-clangd-config.sh --build-dir "build/Release"

# List all available build directories
./scripts/update-clangd-config.sh --list

# Enable verbose output
./scripts/update-clangd-config.sh --auto --verbose

# Show help
./scripts/update-clangd-config.sh --help
```

## Build Scripts

### build-msys2.sh / build-msys2.bat

Scripts for building the project in MSYS2 environment.

### check-msys2-deps.sh

Script to check MSYS2 dependencies.

### test-msys2-build.sh

Script to test MSYS2 build configuration.

## Quick Start

1. **Configure your project:**

   **Windows:**

   ```bash
   cmake --preset Debug-MSYS2
   ```

   **Linux/macOS:**

   ```bash
   cmake --preset Debug
   ```

2. **The .clangd configuration is automatically updated** during CMake configuration.

3. **To manually switch configurations:**

   **Windows:**

   ```powershell
   .\scripts\update-clangd-config.ps1 -Auto
   ```

   **Linux/macOS:**

   ```bash
   ./scripts/update-clangd-config.sh --auto
   ```

4. **To see available configurations:**

   **Windows:**

   ```powershell
   .\scripts\update-clangd-config.ps1 -List
   ```

   **Linux/macOS:**

   ```bash
   ./scripts/update-clangd-config.sh --list
   ```

For detailed information, see [clangd IDE Setup](../docs/setup/clangd-setup.md).
