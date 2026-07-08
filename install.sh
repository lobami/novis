#!/usr/bin/env bash
set -e

# ==============================================================================
# Novis Universal Installer
# ==============================================================================

echo "🚀 Installing Novis Programming Language..."

OS="$(uname -s)"
ARCH="$(uname -m)"

echo "Detected OS: $OS"
echo "Detected Architecture: $ARCH"

# Determine download URL based on OS and architecture
# Currently, since Novis is still experimental and we might not have binaries
# hosted yet, this script will download the source and compile it locally.
# In a future release, this will fetch a pre-compiled binary from GitHub Releases.

INSTALL_DIR="/usr/local/bin"
if [ ! -w "$INSTALL_DIR" ]; then
    INSTALL_DIR="$HOME/.local/bin"
    mkdir -p "$INSTALL_DIR"
fi

TEMP_DIR=$(mktemp -d)
cd "$TEMP_DIR"

echo "📥 Cloning Novis repository..."
git clone --depth 1 https://github.com/lobami/novis.git .

echo "🔨 Compiling Novis from source (requires clang++/g++)..."
make

echo "📦 Installing to $INSTALL_DIR..."
mv novis "$INSTALL_DIR/novis"

cd - > /dev/null
rm -rf "$TEMP_DIR"

echo ""
echo "✅ Novis installed successfully!"
echo "Run 'novis' in your terminal to start the REPL."

if [[ ":$PATH:" != *":$INSTALL_DIR:"* ]]; then
    echo "⚠️  WARNING: $INSTALL_DIR is not in your PATH."
    echo "Please add 'export PATH=\"\$PATH:$INSTALL_DIR\"' to your ~/.bashrc or ~/.zshrc."
fi
