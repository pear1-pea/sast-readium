# MSYS2 Build Guide for SAST Readium

This guide provides comprehensive instructions for building SAST Readium in the MSYS2 environment on Windows.

## Table of Contents

- [Overview](#overview)
- [MSYS2 Installation](#msys2-installation)
- [Dependency Installation](#dependency-installation)
- [Build Configuration](#build-configuration)
- [Building the Project](#building-the-project)
- [vcpkg Integration](#vcpkg-integration)
- [Troubleshooting](#troubleshooting)

## Overview

SAST Readium supports building in the MSYS2 environment, which provides a Unix-like development environment on Windows. This build method offers several advantages:

- **Native Windows binaries** with Unix-like build tools
- **System package management** through pacman
- **Optional vcpkg support** for additional flexibility
- **Consistent build environment** across different Windows systems

## MSYS2 Installation

### 1. Download and Install MSYS2

1. Download MSYS2 from the official website: <https://www.msys2.org/>
2. Run the installer and follow the installation wizard
3. Install to the default location (`C:\msys64`) or remember your custom path

### 2. Update MSYS2

After installation, open the MSYS2 terminal and update the system:

```bash
# Update package database and core packages
pacman -Syu

# Close the terminal when prompted and reopen it
# Then update the rest of the packages
pacman -Su
```

### 3. Choose Your Environment

MSYS2 provides different environments. For building SAST Readium, use:

- **MINGW64** (recommended): For 64-bit Windows applications
- **MINGW32**: For 32-bit Windows applications

Launch the appropriate terminal:

- MSYS2 MINGW64 (for 64-bit builds)
- MSYS2 MINGW32 (for 32-bit builds)

## Dependency Installation

### Automatic Installation

The easiest way to install all required dependencies:

```bash
# Clone the repository (if not already done)
git clone <repository-url>
cd sast-readium

# Make scripts executable
chmod +x scripts/*.sh

# Check and install dependencies automatically
./scripts/check-msys2-deps.sh -i
```

### Manual Installation

If you prefer to install dependencies manually:

```bash
# Update package database
pacman -Sy

# Install build tools
pacman -S mingw-w64-x86_64-cmake \
          mingw-w64-x86_64-ninja \
          mingw-w64-x86_64-gcc \
          mingw-w64-x86_64-pkg-config \
          git

# Install Qt6 packages
pacman -S mingw-w64-x86_64-qt6-base \
          mingw-w64-x86_64-qt6-svg \
          mingw-w64-x86_64-qt6-tools

# Install Poppler for PDF support
pacman -S mingw-w64-x86_64-poppler-qt6
```

**Note**: Replace `x86_64` with `i686` for 32-bit builds.

### Verify Installation

Check that all dependencies are properly installed:

```bash
./scripts/check-msys2-deps.sh
```

## Build Configuration

### CMake Presets

The project includes several CMake presets for MSYS2:

- `Debug-MSYS2`: Debug build using system packages
- `Release-MSYS2`: Release build using system packages
- `Debug-MSYS2-vcpkg`: Debug build using vcpkg
- `Release-MSYS2-vcpkg`: Release build using vcpkg

### Build Options

The build system supports several configuration options:

- `USE_VCPKG`: Enable/disable vcpkg usage (default: OFF in MSYS2)
- `FORCE_VCPKG`: Force vcpkg usage even in MSYS2 (default: OFF)
- `CMAKE_BUILD_TYPE`: Debug or Release

## Building the Project

### Using Build Scripts (Recommended)

#### From MSYS2 Terminal

```bash
# Navigate to project directory
cd /path/to/sast-readium

# Build with default settings (Release, system packages)
./scripts/build-msys2.sh

# Build debug version
./scripts/build-msys2.sh -t Debug

# Clean build with dependency installation
./scripts/build-msys2.sh -c -d

# Build with vcpkg
./scripts/build-msys2.sh -v

# Show all options
./scripts/build-msys2.sh -h
```

#### From Windows Command Prompt

```cmd
REM Navigate to project directory
cd C:\path\to\sast-readium

REM Build with default settings
scripts\build-msys2.bat

REM Build debug version
scripts\build-msys2.bat -t Debug

REM Build with vcpkg
scripts\build-msys2.bat -v

REM Show all options
scripts\build-msys2.bat -h
```

### Manual CMake Build

If you prefer to use CMake directly:

```bash
# Configure (using system packages)
cmake --preset=Release-MSYS2

# Build
cmake --build --preset=Release-MSYS2

# Or configure with vcpkg
cmake --preset=Release-MSYS2-vcpkg
cmake --build --preset=Release-MSYS2-vcpkg
```

### Build Output

After a successful build, you'll find the executable at:

- System packages: `build/Release-MSYS2/app/app.exe`
- With vcpkg: `build/Release-MSYS2-vcpkg/app/app.exe`

## vcpkg Integration

### When to Use vcpkg

- **System packages (default)**: Faster builds, smaller dependencies
- **vcpkg**: More control over dependency versions, easier cross-platform builds

### Setting Up vcpkg

If you want to use vcpkg with MSYS2:

1. Install vcpkg following the official guide
2. Set the `VCPKG_ROOT` environment variable
3. Use the vcpkg build presets or force vcpkg usage

```bash
# Set vcpkg root (add to your shell profile)
export VCPKG_ROOT=/path/to/vcpkg

# Build with vcpkg
./scripts/build-msys2.sh -v
```

### Toggling vcpkg Support

You can control vcpkg usage through:

1. **CMake options**:

   ```bash
   cmake -DUSE_VCPKG=ON ...
   cmake -DFORCE_VCPKG=ON ...
   ```

2. **Build script flags**:

   ```bash
   ./scripts/build-msys2.sh -v  # Use vcpkg
   ```

3. **CMake presets**:

   ```bash
   cmake --preset=Release-MSYS2        # System packages
   cmake --preset=Release-MSYS2-vcpkg  # vcpkg
   ```

## Troubleshooting

### Common Issues

#### 1. "MSYS2 environment not detected"

**Solution**: Make sure you're running the script from an MSYS2 terminal (MINGW64 or MINGW32).

#### 2. "Package not found" errors

**Solution**: Update your package database and install missing packages:

```bash
pacman -Sy
./scripts/check-msys2-deps.sh -i
```

#### 3. Qt6 not found

**Solution**: Ensure Qt6 packages are installed and `MSYSTEM_PREFIX` is set:

```bash
pacman -S mingw-w64-x86_64-qt6-base mingw-w64-x86_64-qt6-svg mingw-w64-x86_64-qt6-tools
echo $MSYSTEM_PREFIX  # Should show /mingw64 or /mingw32
```

#### 4. Poppler-qt6 not found

**Solution**: Install the poppler-qt6 package:

```bash
pacman -S mingw-w64-x86_64-poppler-qt6
```

#### 5. vcpkg build fails

**Solution**: Check that `VCPKG_ROOT` is set correctly:

```bash
echo $VCPKG_ROOT
ls $VCPKG_ROOT/vcpkg.exe  # Should exist
```

### Getting Help

If you encounter issues:

1. Check the dependency status: `./scripts/check-msys2-deps.sh`
2. Verify your MSYS2 environment: `echo $MSYSTEM $MSYSTEM_PREFIX`
3. Check the build logs for specific error messages
4. Try a clean build: `./scripts/build-msys2.sh -c`

### Performance Tips

1. **Use Ninja generator**: Already configured in presets for faster builds
2. **Parallel builds**: Adjust job count with `-j` flag
3. **System packages**: Generally faster than vcpkg for MSYS2 builds
4. **Release builds**: Use `-t Release` for optimized binaries

## Advanced Configuration

### Custom CMake Options

You can pass additional CMake options through environment variables or by modifying the presets:

```bash
# Example: Enable additional Qt modules
export CMAKE_ARGS="-DQt6_DIR=/mingw64/lib/cmake/Qt6"
./scripts/build-msys2.sh
```

### Cross-compilation

MSYS2 supports cross-compilation between 32-bit and 64-bit:

```bash
# For 32-bit from 64-bit environment
pacman -S mingw-w64-i686-toolchain
# Then use MINGW32 environment
```

This completes the MSYS2 build guide. The build system is designed to be flexible and work seamlessly in both MSYS2 and traditional Windows development environments.
