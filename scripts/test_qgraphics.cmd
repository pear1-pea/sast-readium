@echo off
echo Testing QGraphics PDF Support
echo ==============================

set PROJECT_ROOT=%~dp0..
set BUILD_DIR=%PROJECT_ROOT%\build

echo Project root: %PROJECT_ROOT%
echo Build directory: %BUILD_DIR%

:: Test 1: Build with QGraphics DISABLED
echo.
echo [1/2] Testing QGraphics DISABLED build...
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

cmake -DENABLE_QGRAPHICS_PDF_SUPPORT=OFF .. >nul 2>&1
if %errorlevel% equ 0 (
    cmake --build . --config Release >nul 2>&1
    if %errorlevel% equ 0 (
        echo ✓ QGraphics DISABLED: BUILD SUCCESS
        set TEST1_RESULT=PASS
    ) else (
        echo ✗ QGraphics DISABLED: BUILD FAILED
        set TEST1_RESULT=FAIL
    )
) else (
    echo ✗ QGraphics DISABLED: CONFIG FAILED
    set TEST1_RESULT=FAIL
)

:: Test 2: Build with QGraphics ENABLED
echo.
echo [2/2] Testing QGraphics ENABLED build...
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

cmake -DENABLE_QGRAPHICS_PDF_SUPPORT=ON .. >nul 2>&1
if %errorlevel% equ 0 (
    cmake --build . --config Release >nul 2>&1
    if %errorlevel% equ 0 (
        echo ✓ QGraphics ENABLED: BUILD SUCCESS
        set TEST2_RESULT=PASS
    ) else (
        echo ✗ QGraphics ENABLED: BUILD FAILED
        set TEST2_RESULT=FAIL
    )
) else (
    echo ✗ QGraphics ENABLED: CONFIG FAILED
    set TEST2_RESULT=FAIL
)

:: Summary
echo.
echo Test Summary:
echo =============
echo QGraphics DISABLED: %TEST1_RESULT%
echo QGraphics ENABLED:  %TEST2_RESULT%

if "%TEST1_RESULT%"=="PASS" if "%TEST2_RESULT%"=="PASS" (
    echo.
    echo ✓ ALL TESTS PASSED
    echo Both configurations build successfully!
    exit /b 0
) else (
    echo.
    echo ✗ SOME TESTS FAILED
    exit /b 1
)
