# Restart clangd Language Server Script
# This script helps restart the clangd language server in VSCode to apply configuration changes

Write-Host "=== Restarting clangd Language Server ===" -ForegroundColor Green
Write-Host ""

Write-Host "Configuration changes applied to .clangd:" -ForegroundColor Cyan
Write-Host "✓ Disabled strict missing-includes warnings" -ForegroundColor Green
Write-Host "✓ Added Qt-specific compiler defines" -ForegroundColor Green
Write-Host "✓ Suppressed 'missing-includes' diagnostics" -ForegroundColor Green
Write-Host "✓ Removed misc-include-cleaner from ClangTidy" -ForegroundColor Green
Write-Host "✓ Enabled AllScopes completion for better Qt symbol resolution" -ForegroundColor Green
Write-Host ""

Write-Host "To apply these changes in VSCode:" -ForegroundColor Yellow
Write-Host "1. Open Command Palette (Ctrl+Shift+P / Cmd+Shift+P)"
Write-Host "2. Type 'clangd: Restart language server'"
Write-Host "3. Press Enter to restart clangd"
Write-Host ""

Write-Host "Alternative methods:" -ForegroundColor Yellow
Write-Host "• Reload VSCode window (Ctrl+Shift+P → 'Developer: Reload Window')"
Write-Host "• Close and reopen the workspace"
Write-Host ""

Write-Host "After restarting, the following warnings should be resolved:" -ForegroundColor Cyan
Write-Host "✓ 'No header providing Qt::AlignCenter is directly included'"
Write-Host "✓ Other Qt constant missing-includes warnings"
Write-Host "✓ Transitive include warnings for Qt headers"
Write-Host ""

Write-Host "If issues persist:" -ForegroundColor Yellow
Write-Host "1. Check that compile_commands.json exists in build directory"
Write-Host "2. Verify Qt headers are properly installed"
Write-Host "3. Ensure CMake configuration includes Qt properly"
Write-Host ""

Write-Host "Configuration file location: .clangd" -ForegroundColor Cyan
Write-Host "For more details, see the updated .clangd file in the project root."
