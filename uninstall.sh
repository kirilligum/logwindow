#!/usr/bin/env bash
# Uninstall script for logwindow

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

BINARY_NAME="logwindow"
INSTALL_DIR="$HOME/.local/bin"
BINARY_PATH="$INSTALL_DIR/$BINARY_NAME"

echo "Uninstalling logwindow..."
echo ""

if [[ ! -f "$BINARY_PATH" ]]; then
    echo -e "${YELLOW}Warning: $BINARY_PATH not found${NC}"
    echo "logwindow may not be installed, or it was installed to a different location"
    exit 0
fi

# Check if any logwindow processes are running
if pgrep -x "$BINARY_NAME" > /dev/null; then
    echo -e "${YELLOW}Warning: logwindow processes are currently running${NC}"
    echo ""
    echo "Running processes:"
    pgrep -a "$BINARY_NAME"
    echo ""
    read -p "Stop all logwindow processes? [y/N] " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        pkill -x "$BINARY_NAME"
        echo -e "${GREEN}✓${NC} Stopped all logwindow processes"
    else
        echo "Continuing with uninstall (processes will keep running)"
    fi
    echo ""
fi

# Remove binary
echo "→ Removing $BINARY_PATH..."
rm "$BINARY_PATH"
echo -e "${GREEN}✓${NC} Binary removed"

echo ""
echo -e "${GREEN}Uninstallation complete!${NC}"
echo ""
echo "To reinstall, run: ./install.sh"
