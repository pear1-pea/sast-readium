# Xmake Build System

The Xmake build system provides a modern, Lua-based alternative to CMake with simpler configuration syntax and built-in package management.

## Current Status

The Xmake build system has been **successfully integrated** into SAST Readium with comprehensive configuration and documentation. However, there are **compatibility issues** in certain environments that may affect functionality.

## Documentation in this Section

### [Build Guide](xmake-build.md)

Comprehensive instructions for building SAST Readium using Xmake:

- Installation instructions for all platforms
- Basic and advanced build configurations
- Platform-specific setup (Windows, Linux, macOS)
- Troubleshooting common issues
- IDE integration

### [Status Report](xmake-status.md)

Current implementation status including:

- What has been accomplished
- Known issues and limitations
- Compatibility considerations
- Workarounds for common problems

### [Final Report](xmake-final-report.md)

Implementation summary and conclusions:

- Technical achievements
- Performance comparisons
- Lessons learned
- Future recommendations

## Key Features

### ✅ Implemented

- Complete build configuration
- Qt6 integration with MOC/UIC/RCC
- Cross-platform support
- Dependency management via pkg-config
- Debug/Release build modes

### ⚠️ Known Issues

- Qt detection assertion errors in MSYS2 environment
- Mixed environment compatibility challenges
- Some Qt rules may not work correctly

## Quick Start

```bash
# Install xmake (see Build Guide for platform-specific instructions)
# Configure and build
xmake f -m release
xmake

# Run the application
xmake run
```

## Recommendations

- **For production builds**: Use CMake build system
- **For experimentation**: Xmake provides a good alternative
- **For MSYS2 users**: Consider using CMake until compatibility issues are resolved

## Next Steps

1. Read the [Build Guide](xmake-build.md) for detailed setup instructions
2. Check the [Status Report](xmake-status.md) for current limitations
3. Review the [Final Report](xmake-final-report.md) for implementation insights
