# Build Systems

SAST Readium supports multiple build systems to provide flexibility for different development preferences and environments.

## Available Build Systems

### CMake (Primary)

The primary and most mature build system with comprehensive configuration and extensive platform support.

**Advantages:**

- Mature and well-tested
- Extensive platform support
- Comprehensive configuration options
- Full CI/CD integration

### Xmake (Alternative)

A modern Lua-based build system that offers simpler configuration syntax and built-in package management.

**Advantages:**

- Simpler configuration syntax
- Built-in package management
- Faster builds with automatic caching
- Cross-platform consistency

## Documentation in this Section

### [Build System Comparison](build-system-comparison.md)

Detailed feature-by-feature comparison between CMake and Xmake implementations, including:

- Core build features
- Dependency management
- Platform support
- Qt integration
- Performance characteristics

### [Xmake Build System](xmake/)

Complete documentation for the Xmake build system:

- [Build Guide](xmake/xmake-build.md) - Installation and usage instructions
- [Status Report](xmake/xmake-status.md) - Current implementation status
- [Final Report](xmake/xmake-final-report.md) - Implementation summary and conclusions

## Choosing a Build System

### Use CMake if:

- You need maximum stability and platform support
- You're working in a production environment
- You prefer the established CMake ecosystem
- You need the most comprehensive configuration options

### Use Xmake if:

- You prefer simpler, more readable configuration files
- You want built-in package management
- You're experimenting or prototyping
- You value faster build times

## Next Steps

1. Review the [Build System Comparison](build-system-comparison.md) to understand the differences
2. Choose your preferred build system
3. Follow the appropriate setup guides in [Setup & Configuration](../setup/)
