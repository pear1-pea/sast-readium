cmake_minimum_required(VERSION 3.28)
project(sast-readium LANGUAGES CXX VERSION 0.1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# MSYS2 and vcpkg configuration
option(USE_VCPKG "Use vcpkg for dependency management" OFF)
option(FORCE_VCPKG "Force vcpkg usage even in MSYS2" OFF)

# clangd configuration
option(ENABLE_CLANGD_CONFIG "Enable automatic clangd configuration updates" ON)

# QGraphics PDF support
option(ENABLE_QGRAPHICS_PDF_SUPPORT "Enable QGraphics-based PDF rendering support" OFF)

# Testing support
option(BUILD_TESTING "Build tests" ON)

# Detect MSYS2 environment
if(WIN32 AND DEFINED ENV{MSYSTEM})
    set(MSYS2_DETECTED TRUE)
    message(STATUS "MSYS2 environment detected: $ENV{MSYSTEM}")
else()
    set(MSYS2_DETECTED FALSE)
endif()

# Determine whether to use vcpkg
if(FORCE_VCPKG)
    set(USE_VCPKG_INTERNAL TRUE)
    message(STATUS "vcpkg usage forced via FORCE_VCPKG option")
elseif(MSYS2_DETECTED AND NOT USE_VCPKG)
    set(USE_VCPKG_INTERNAL FALSE)
    message(STATUS "MSYS2 detected - using system packages instead of vcpkg")
else()
    set(USE_VCPKG_INTERNAL ${USE_VCPKG})
    if(USE_VCPKG_INTERNAL)
        message(STATUS "Using vcpkg for dependency management")
    else()
        message(STATUS "Using system packages for dependency management")
    endif()
endif()

# Configure dependency finding based on build environment
if(USE_VCPKG_INTERNAL)
    # vcpkg mode - use CONFIG mode for all packages
    find_package(
        Qt6
        COMPONENTS Core Gui Widgets Svg LinguistTools Concurrent TextToSpeech
        CONFIG
        REQUIRED
    )
    find_package(spdlog CONFIG REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(POPPLER_QT6 REQUIRED IMPORTED_TARGET poppler-qt6)
else()
    # System packages mode (MSYS2 or other system package managers)
    if(MSYS2_DETECTED)
        # MSYS2-specific package finding
        message(STATUS "Configuring for MSYS2 environment")

        # Set Qt6 installation path for MSYS2
        if(DEFINED ENV{MSYSTEM_PREFIX})
            list(APPEND CMAKE_PREFIX_PATH "$ENV{MSYSTEM_PREFIX}")
            message(STATUS "Added MSYSTEM_PREFIX to CMAKE_PREFIX_PATH: $ENV{MSYSTEM_PREFIX}")
        endif()

        # Find Qt6 using system installation
        find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg LinguistTools Concurrent TextToSpeech)

        # Find spdlog using system installation
        find_package(spdlog REQUIRED)

        # Find poppler-qt6 using pkg-config
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(POPPLER_QT6 REQUIRED IMPORTED_TARGET poppler-qt6)

        # MSYS2 specific configurations
        if(CMAKE_BUILD_TYPE STREQUAL "Release")
            # Strip debug symbols in release builds for smaller binaries
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s")
        endif()
    else()
        # Generic system package finding
        find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg LinguistTools Concurrent TextToSpeech)
        find_package(spdlog REQUIRED)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(POPPLER_QT6 REQUIRED IMPORTED_TARGET poppler-qt6)
    endif()
endif()

qt_standard_project_setup(I18N_TRANSLATED_LANGUAGES zh)

add_subdirectory(app)

# Add tests if enabled
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# Auto-update .clangd configuration for the current build directory
if(CMAKE_EXPORT_COMPILE_COMMANDS AND ENABLE_CLANGD_CONFIG)
    # Calculate relative path from source directory to build directory
    file(RELATIVE_PATH BUILD_DIR_RELATIVE ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR})

    # Convert Windows backslashes to forward slashes for consistency
    string(REPLACE "\\" "/" BUILD_DIR_RELATIVE ${BUILD_DIR_RELATIVE})

    # Determine the appropriate script based on the platform
    if(WIN32)
        # Windows: Try PowerShell first, then batch script
        find_program(POWERSHELL_PATH powershell)

        if(POWERSHELL_PATH)
            set(UPDATE_SCRIPT_CMD ${POWERSHELL_PATH} -ExecutionPolicy Bypass -File
                "${CMAKE_SOURCE_DIR}/scripts/update-clangd-config.ps1"
                -BuildDir "${BUILD_DIR_RELATIVE}")
            set(UPDATE_SCRIPT_CMD_VERBOSE ${POWERSHELL_PATH} -ExecutionPolicy Bypass -File
                "${CMAKE_SOURCE_DIR}/scripts/update-clangd-config.ps1"
                -BuildDir "${BUILD_DIR_RELATIVE}" -Verbose)
        else()
            # Fallback to batch script
            set(UPDATE_SCRIPT_CMD "${CMAKE_SOURCE_DIR}/scripts/update-clangd-config.bat" "${BUILD_DIR_RELATIVE}")
            set(UPDATE_SCRIPT_CMD_VERBOSE "${CMAKE_SOURCE_DIR}/scripts/update-clangd-config.bat" "${BUILD_DIR_RELATIVE}")
        endif()
    else()
        # Unix-like systems (Linux, macOS): Use shell script
        find_program(BASH_PATH bash)
        if(BASH_PATH)
            set(UPDATE_SCRIPT_CMD ${BASH_PATH} "${CMAKE_SOURCE_DIR}/scripts/update-clangd-config.sh"
                --build-dir "${BUILD_DIR_RELATIVE}")
            set(UPDATE_SCRIPT_CMD_VERBOSE ${BASH_PATH} "${CMAKE_SOURCE_DIR}/scripts/update-clangd-config.sh"
                --build-dir "${BUILD_DIR_RELATIVE}" --verbose)
        else()
            # Fallback to sh
            set(UPDATE_SCRIPT_CMD sh "${CMAKE_SOURCE_DIR}/scripts/update-clangd-config.sh"
                --build-dir "${BUILD_DIR_RELATIVE}")
            set(UPDATE_SCRIPT_CMD_VERBOSE sh "${CMAKE_SOURCE_DIR}/scripts/update-clangd-config.sh"
                --build-dir "${BUILD_DIR_RELATIVE}" --verbose)
        endif()
    endif()

    # Add custom target to update .clangd configuration automatically after compile_commands.json is generated
    if(DEFINED UPDATE_SCRIPT_CMD)
        # 依赖 compile_commands.json，自动在其生成后执行
        add_custom_target(update_clangd_config ALL
            COMMAND ${UPDATE_SCRIPT_CMD_VERBOSE}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Updating .clangd configuration for build directory: ${BUILD_DIR_RELATIVE}"
            DEPENDS ${CMAKE_BINARY_DIR}/compile_commands.json
            VERBATIM
        )
    else()
        message(WARNING "No suitable script interpreter found for updating .clangd configuration")
    endif()
else()
    if(NOT CMAKE_EXPORT_COMPILE_COMMANDS)
        message(STATUS "clangd configuration disabled: CMAKE_EXPORT_COMPILE_COMMANDS is OFF")
    elseif(NOT ENABLE_CLANGD_CONFIG)
        message(STATUS "clangd configuration disabled: ENABLE_CLANGD_CONFIG is OFF")
    endif()
endif()

# Print general build configuration information
message(STATUS "=== Build Configuration Summary ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Export compile commands: ${CMAKE_EXPORT_COMPILE_COMMANDS}")
message(STATUS "clangd auto-config: ${ENABLE_CLANGD_CONFIG}")
if(USE_VCPKG_INTERNAL)
    message(STATUS "Dependency management: vcpkg")
else()
    message(STATUS "Dependency management: system packages")
endif()
message(STATUS "=====================================")

# MSYS2-specific post-build configurations
if(MSYS2_DETECTED)
    # Print build information
    message(STATUS "=== MSYS2 Build Configuration ===")
    message(STATUS "MSYSTEM: $ENV{MSYSTEM}")
    message(STATUS "MSYSTEM_PREFIX: $ENV{MSYSTEM_PREFIX}")
    message(STATUS "Using vcpkg: ${USE_VCPKG_INTERNAL}")
    message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
    message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER}")
    message(STATUS "clangd auto-config: ${ENABLE_CLANGD_CONFIG}")
    message(STATUS "================================")

    # Set RPATH for MSYS2 libraries
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
    endif()
endif()
