@echo off
setlocal enabledelayedexpansion

echo ================================================================================
echo QGraphics PDF Support Test Runner for Windows
echo ================================================================================

set PROJECT_ROOT=%~dp0..
set BUILD_DIR=%PROJECT_ROOT%\build
set TEST_RESULTS_FILE=%PROJECT_ROOT%\test_results.txt

echo Project root: %PROJECT_ROOT%
echo Build directory: %BUILD_DIR%
echo.

:: Check if CMake is available
cmake --version >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: CMake not found. Please install CMake and add it to PATH.
    exit /b 1
)

echo CMake found: OK
echo.

:: Function to clean build directory
:clean_build
echo Cleaning build directory...
if exist "%BUILD_DIR%" (
    rmdir /s /q "%BUILD_DIR%"
)
mkdir "%BUILD_DIR%"
goto :eof

:: Function to build with CMake
:build_cmake
set QGRAPHICS_SETTING=%1

echo ================================================================================
echo Building with CMake (QGraphics: %QGRAPHICS_SETTING%)
echo ================================================================================

cd /d "%BUILD_DIR%"

:: Configure
echo Configuring...
cmake -DENABLE_QGRAPHICS_PDF_SUPPORT=%QGRAPHICS_SETTING% .. >cmake_config.log 2>&1
if %errorlevel% neq 0 (
    echo ERROR: CMake configuration failed
    type cmake_config.log
    set BUILD_SUCCESS=0
    goto :eof
)

echo Configuration successful

:: Build
echo Building...
cmake --build . --config Release >cmake_build.log 2>&1
if %errorlevel% neq 0 (
    echo ERROR: CMake build failed
    type cmake_build.log
    set BUILD_SUCCESS=0
    goto :eof
)

echo Build successful
set BUILD_SUCCESS=1
goto :eof

:: Main execution starts here
echo Starting comprehensive QGraphics PDF support testing
echo.

:: Initialize results file
echo QGraphics PDF Support Test Results > "%TEST_RESULTS_FILE%"
echo Generated: %date% %time% >> "%TEST_RESULTS_FILE%"
echo ======================================== >> "%TEST_RESULTS_FILE%"

set OVERALL_SUCCESS=1

:: Test Configuration 1: QGraphics DISABLED
call :clean_build
call :build_cmake "OFF"

echo Configuration: CMake_QGraphics_Disabled >> "%TEST_RESULTS_FILE%"
if %BUILD_SUCCESS% equ 1 (
    echo Build: SUCCESS >> "%TEST_RESULTS_FILE%"
    echo ✓ QGraphics DISABLED build: SUCCESS
) else (
    echo Build: FAILED >> "%TEST_RESULTS_FILE%"
    echo ✗ QGraphics DISABLED build: FAILED
    set OVERALL_SUCCESS=0
)

:: Test Configuration 2: QGraphics ENABLED
call :clean_build
call :build_cmake "ON"

echo Configuration: CMake_QGraphics_Enabled >> "%TEST_RESULTS_FILE%"
if %BUILD_SUCCESS% equ 1 (
    echo Build: SUCCESS >> "%TEST_RESULTS_FILE%"
    echo ✓ QGraphics ENABLED build: SUCCESS
) else (
    echo Build: FAILED >> "%TEST_RESULTS_FILE%"
    echo ✗ QGraphics ENABLED build: FAILED
    set OVERALL_SUCCESS=0
)

:: Generate summary
echo.
echo ================================================================================
echo TEST SUMMARY
echo ================================================================================

if %OVERALL_SUCCESS% equ 1 (
    echo ✓ SUCCESS: All configurations built successfully
    echo Overall result: SUCCESS >> "%TEST_RESULTS_FILE%"
    echo.
    echo Both QGraphics enabled and disabled configurations compile correctly.
    echo The conditional compilation is working as expected.
    exit /b 0
) else (
    echo ✗ FAILURE: Some configurations failed
    echo Overall result: FAILED >> "%TEST_RESULTS_FILE%"
    echo.
    echo Check the build logs above for details.
    exit /b 1
)
