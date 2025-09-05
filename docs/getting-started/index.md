# Getting Started

This section contains essential information to get you up and running with SAST Readium.

## Overview

SAST Readium is a Qt6-based PDF reader application that supports multiple platforms and build systems. Before building the application, you'll need to understand the dependency management strategy and platform-specific requirements.

## Documentation in this Section

### [Dependency Management](dependency-management.md)

Learn about the tiered dependency management approach that prioritizes system packages for better performance and reliability. This guide covers:

- System packages vs vcpkg strategy
- Platform-specific dependency installation
- Build configuration options

### [Platform Support](platform-support.md)

Comprehensive platform support matrix including:

- Fully supported platforms with CI testing
- Cross-compilation targets
- Architecture-specific considerations
- Platform-specific build instructions

## Quick Start Recommendations

1. **Start with [Platform Support](platform-support.md)** to understand what's supported on your platform
2. **Review [Dependency Management](dependency-management.md)** to understand the build strategy
3. **Choose your build approach** from the [Build Systems](../build-systems/) section
4. **Configure your development environment** using the [Setup & Configuration](../setup/) guides

## Next Steps

After reviewing this section, proceed to:

- [Build Systems](../build-systems/) - Choose between CMake and Xmake
- [Setup & Configuration](../setup/) - Configure your development environment
