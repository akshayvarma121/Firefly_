#!/bin/bash

# Firefly Installer for macOS / Linux

set -e

# Detect OS
OS="$(uname -s)"
ARCH="$(uname -m)"

if [ "$OS" = "Darwin" ]; then
    ASSET_NAME="firefly-macos"
elif [ "$OS" = "Linux" ]; then
    ASSET_NAME="firefly-linux"
else
    echo -e "\n  \033[31m[FAILED] Unsupported OS: $OS\033[0m\n"
    exit 1
fi

INSTALL_DIR="$HOME/.local/bin"
EXE_PATH="$INSTALL_DIR/firefly"
DOWNLOAD_URL="https://github.com/akshayvarma121/Firefly_solver/releases/download/v0.1.0/$ASSET_NAME"

echo ""
echo -e "  \033[33mDownloading Firefly Solver Engine...\033[0m"
echo ""

mkdir -p "$INSTALL_DIR"

if curl -L --progress-bar "$DOWNLOAD_URL" -o "$EXE_PATH"; then
    chmod +x "$EXE_PATH"
    echo ""
    echo -e "  \033[32m[OK] Firefly installed successfully.\033[0m"
    echo ""
    echo -e "  Location: $EXE_PATH"
    echo ""
    
    # Check if ~/.local/bin is in PATH
    if [[ ":$PATH:" != *":$INSTALL_DIR:"* ]]; then
        echo -e "  \033[33m[WARNING] $INSTALL_DIR is not in your PATH.\033[0m"
        echo "  Add it to your ~/.bashrc or ~/.zshrc:"
        echo "    export PATH=\"\$HOME/.local/bin:\$PATH\""
        echo ""
    fi
    
    echo -e "  Run '\033[1mfirefly home\033[0m' to see all commands."
    echo ""
else
    echo ""
    echo -e "  \033[31m[FAILED] Download failed. Ensure you have an internet connection and curl is installed.\033[0m"
    echo ""
    exit 1
fi
