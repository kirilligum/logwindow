#!/usr/bin/env bash
# Install script for logwindow

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

BINARY_NAME="logwindow"
INSTALL_DIR="$HOME/.local/bin"
SOURCE_FILE="main.cc"

echo "Installing logwindow..."
echo ""

# Check if source file exists
if [[ ! -f "$SOURCE_FILE" ]]; then
    echo -e "${RED}Error: $SOURCE_FILE not found${NC}"
    echo "Please run this script from the logwindow project directory"
    exit 1
fi

# Detect compiler
if command -v clang++ &> /dev/null; then
    COMPILER="clang++"
elif command -v g++ &> /dev/null; then
    COMPILER="g++"
else
    echo -e "${RED}Error: No C++ compiler found${NC}"
    echo "Please install clang++ or g++"
    exit 1
fi

echo "→ Compiling with $COMPILER..."
$COMPILER -std=c++20 -O3 -o "$BINARY_NAME" "$SOURCE_FILE"

if [[ ! -f "$BINARY_NAME" ]]; then
    echo -e "${RED}Error: Compilation failed${NC}"
    exit 1
fi

echo -e "${GREEN}✓${NC} Compilation successful"

# Create installation directory if it doesn't exist
if [[ ! -d "$INSTALL_DIR" ]]; then
    echo "→ Creating $INSTALL_DIR..."
    mkdir -p "$INSTALL_DIR"
    echo -e "${GREEN}✓${NC} Directory created"
fi

# Copy binary
echo "→ Installing to $INSTALL_DIR/$BINARY_NAME..."
cp "$BINARY_NAME" "$INSTALL_DIR/$BINARY_NAME"
chmod +x "$INSTALL_DIR/$BINARY_NAME"
echo -e "${GREEN}✓${NC} Binary installed"

# Clean up local binary
rm "$BINARY_NAME"

# Check if ~/.local/bin is in PATH
if [[ ":$PATH:" != *":$INSTALL_DIR:"* ]]; then
    echo ""
    echo -e "${YELLOW}Warning: $INSTALL_DIR is not in your PATH${NC}"
    echo ""
    echo "Add it to your PATH by adding this line to your shell config:"
    echo ""

    # Detect shell and provide appropriate instructions
    if [[ -n "$FISH_VERSION" ]] || [[ "$SHELL" == *"fish"* ]]; then
        echo "  fish_add_path $INSTALL_DIR"
        echo ""
        echo "Or add to ~/.config/fish/config.fish:"
        echo "  set -gx PATH $INSTALL_DIR \$PATH"
    elif [[ "$SHELL" == *"zsh"* ]]; then
        echo "  echo 'export PATH=\"$INSTALL_DIR:\$PATH\"' >> ~/.zshrc"
        echo "  source ~/.zshrc"
    else
        echo "  echo 'export PATH=\"$INSTALL_DIR:\$PATH\"' >> ~/.bashrc"
        echo "  source ~/.bashrc"
    fi
    echo ""
else
    echo -e "${GREEN}✓${NC} $INSTALL_DIR is in your PATH"
fi

echo ""
echo -e "${GREEN}Installation complete!${NC}"
echo ""
echo "Usage: logwindow <logfile> [options]"
echo "Example: firebase emulators:start 2>&1 | logwindow firebase.log &"
echo ""
echo "Run 'logwindow --help' for more options"
echo "Run './uninstall.sh' to uninstall"
