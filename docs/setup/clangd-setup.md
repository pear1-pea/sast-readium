# Cross-Platform Support for clangd Configuration

This document describes the cross-platform support added to the sast-readium project for automatic clangd configuration management.

## Overview

The project now includes comprehensive cross-platform support for automatically configuring clangd to work with different build configurations. This ensures that developers on Windows, Linux, and macOS can all benefit from enhanced IDE features like code completion, error checking, and navigation.

## Supported Platforms

### Windows

- **PowerShell Script**: `scripts/update-clangd-config.ps1` (Recommended)
- **Batch Script**: `scripts/update-clangd-config.bat` (Alternative)
- **CMake Integration**: Automatic detection and execution of appropriate script

### Linux

- **Shell Script**: `scripts/update-clangd-config.sh`
- **Makefile**: Convenient targets for common tasks
- **CMake Integration**: Automatic bash/sh detection and execution

### macOS

- **Shell Script**: `scripts/update-clangd-config.sh` (same as Linux)
- **Makefile**: Convenient targets for common tasks
- **CMake Integration**: Automatic bash/sh detection and execution

## Features

### Automatic Platform Detection

The CMake build system automatically detects the platform and uses the appropriate script:

- Windows: PowerShell → Batch (fallback)
- Unix-like: bash → sh (fallback)

### Consistent Interface

All scripts provide the same functionality across platforms:

- Auto-detection of best build configuration
- Manual specification of build directory
- Listing of available configurations
- Verbose output for debugging

### CMake Integration

When you run `cmake --preset <preset>`, the system automatically:

1. Detects the current platform
2. Selects the appropriate script
3. Updates the `.clangd` configuration file
4. Reports success/failure status

## Script Comparison

| Feature             | Windows PowerShell | Windows Batch | Unix Shell |
| ------------------- | ------------------ | ------------- | ---------- |
| Auto-detection      | ✅                 | ✅            | ✅         |
| Manual selection    | ✅                 | ✅            | ✅         |
| List configurations | ✅                 | ✅            | ✅         |
| Verbose output      | ✅                 | ❌            | ✅         |
| Help message        | ✅                 | ✅            | ✅         |
| Error handling      | ✅                 | ✅            | ✅         |

## Usage Examples

### Windows (PowerShell)

```powershell
# Auto-detect best configuration
.\scripts\update-clangd-config.ps1 -Auto

# Use specific build directory
.\scripts\update-clangd-config.ps1 -BuildDir "build/Release-MSYS2"

# List available configurations
.\scripts\update-clangd-config.ps1 -List

# Enable verbose output
.\scripts\update-clangd-config.ps1 -Auto -Verbose
```

### Windows (Batch)

```cmd
# Auto-detect best configuration
scripts\update-clangd-config.bat --auto

# Use specific build directory
scripts\update-clangd-config.bat build/Release-MSYS2

# List available configurations
scripts\update-clangd-config.bat --list
```

### Linux/macOS (Shell)

```bash
# Auto-detect best configuration
./scripts/update-clangd-config.sh --auto

# Use specific build directory
./scripts/update-clangd-config.sh --build-dir "build/Release"

# List available configurations
./scripts/update-clangd-config.sh --list

# Enable verbose output
./scripts/update-clangd-config.sh --auto --verbose

# Show help
./scripts/update-clangd-config.sh --help
```

### Linux/macOS (Makefile)

```bash
# Setup development environment
make dev

# Update clangd configuration
make clangd-auto

# List available configurations
make clangd-list

# Set specific configuration
make clangd-debug    # or clangd-release
```

## Build Configuration Support

All platforms support the same build configurations defined in `CMakePresets.json`:

- `build/Debug` - Debug build (Unix/Linux/macOS)
- `build/Release` - Release build (Unix/Linux/macOS)
- `build/Debug-MSYS2` - Debug build (Windows MSYS2)
- `build/Release-MSYS2` - Release build (Windows MSYS2)
- `build/Debug-MSYS2-vcpkg` - Debug build (Windows MSYS2 + vcpkg)
- `build/Release-MSYS2-vcpkg` - Release build (Windows MSYS2 + vcpkg)
- `build/Debug-Windows` - Debug build (Windows Visual Studio)
- `build/Release-Windows` - Release build (Windows Visual Studio)

## Configuration Options

### Enabling/Disabling clangd Auto-Configuration

The project includes a CMake option to control automatic clangd configuration:

```cmake
option(ENABLE_CLANGD_CONFIG "Enable automatic clangd configuration updates" ON)
```

**To disable clangd auto-configuration:**

```bash
cmake --preset Debug-MSYS2 -DENABLE_CLANGD_CONFIG=OFF
```

**To re-enable clangd auto-configuration:**

```bash
cmake --preset Debug-MSYS2 -DENABLE_CLANGD_CONFIG=ON
```

### Force Override

When `ENABLE_CLANGD_CONFIG` is disabled, the scripts will refuse to update the `.clangd` file. You can override this behavior using the `--force` parameter:

**Windows:**

```powershell
# Force update even when disabled
.\scripts\update-clangd-config.ps1 -Auto -Force
.\scripts\update-clangd-config.ps1 -BuildDir "build/Release-MSYS2" -Force
```

**Linux/macOS:**

```bash
# Force update even when disabled
./scripts/update-clangd-config.sh --auto --force
./scripts/update-clangd-config.sh --build-dir "build/Release" --force
```

## Implementation Details

### CMake Integration

The `CMakeLists.txt` file includes platform-specific logic:

1. Detects Windows vs Unix-like systems
2. Finds appropriate script interpreter (PowerShell, bash, sh)
3. Executes the correct script with proper arguments
4. Reports results to the user

### Script Architecture

All scripts follow a consistent pattern:

1. Parse command-line arguments
2. Validate build directories
3. Update `.clangd` configuration file
4. Provide user feedback

### Error Handling

Robust error handling across all platforms:

- Missing build directories
- Invalid script arguments
- File permission issues
- Missing dependencies

## Benefits

1. **Consistent Developer Experience**: Same functionality across all platforms
2. **Automatic Configuration**: No manual `.clangd` file editing required
3. **IDE Integration**: Enhanced code completion and error checking
4. **Build System Integration**: Automatic updates during CMake configuration
5. **Flexible Usage**: Both automatic and manual configuration options

## Troubleshooting

### Common Issues

- **clangd not working**: Run `.\scripts\update-clangd-config.ps1 -List` to see available configurations
- **No build directories found**: Run CMake configuration first: `cmake --preset Debug-MSYS2`
- **Wrong build directory**: Use `.\scripts\update-clangd-config.ps1 -Auto` to auto-detect the correct one
- **Script execution errors**: Ensure PowerShell execution policy allows script execution

### C++20 Modules Warnings

If you see warnings like:

```
Unknown argument: '-fmodules-ts'
Unknown argument: '-fmodule-mapper=...'
Unknown argument: '-fdeps-format=p1689r5'
```

These are C++20 modules-related compiler flags that clangd doesn't fully support yet. The project's `.clangd` configuration automatically filters out these problematic flags:

```yaml
CompileFlags:
  CompilationDatabase: build/Debug-MSYS2
  Remove:
    # Remove C++20 modules flags that clangd doesn't support yet
    - "-fmodules-ts"
    - "-fmodule-mapper=*"
    - "-fdeps-format=*"
    # Remove other potentially problematic flags
    - "-MD"
    - "-x"
```

The configuration scripts automatically include these filters when creating or updating the `.clangd` file.

## Future Enhancements

Potential improvements for future versions:

- GUI configuration tool
- Integration with more build systems
- Support for additional IDEs
- Configuration templates for different project types
