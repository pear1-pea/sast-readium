#!/bin/bash
# Restart clangd Language Server Script
# This script helps restart the clangd language server in VSCode to apply configuration changes

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Restarting clangd Language Server ===${NC}"
echo ""

echo -e "${CYAN}Configuration changes applied to .clangd:${NC}"
echo -e "${GREEN}✓ Disabled strict missing-includes warnings${NC}"
echo -e "${GREEN}✓ Added Qt-specific compiler defines${NC}"
echo -e "${GREEN}✓ Suppressed 'missing-includes' diagnostics${NC}"
echo -e "${GREEN}✓ Removed misc-include-cleaner from ClangTidy${NC}"
echo -e "${GREEN}✓ Enabled AllScopes completion for better Qt symbol resolution${NC}"
echo ""

echo -e "${YELLOW}To apply these changes in VSCode:${NC}"
echo "1. Open Command Palette (Ctrl+Shift+P / Cmd+Shift+P)"
echo "2. Type 'clangd: Restart language server'"
echo "3. Press Enter to restart clangd"
echo ""

echo -e "${YELLOW}Alternative methods:${NC}"
echo "• Reload VSCode window (Ctrl+Shift+P → 'Developer: Reload Window')"
echo "• Close and reopen the workspace"
echo ""

echo -e "${CYAN}After restarting, the following warnings should be resolved:${NC}"
echo "✓ 'No header providing Qt::AlignCenter is directly included'"
echo "✓ Other Qt constant missing-includes warnings"
echo "✓ Transitive include warnings for Qt headers"
echo ""

echo -e "${YELLOW}If issues persist:${NC}"
echo "1. Check that compile_commands.json exists in build directory"
echo "2. Verify Qt headers are properly installed"
echo "3. Ensure CMake configuration includes Qt properly"
echo ""

echo -e "${CYAN}Configuration file location: .clangd${NC}"
echo "For more details, see the updated .clangd file in the project root."
