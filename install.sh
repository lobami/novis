#!/usr/bin/env bash
# ==============================================================================
# Novis Universal Installer
# https://github.com/lobami/novis
# Created by Loth Mejía Martínez · México · 2026
# ==============================================================================
# Installs the Novis toolchain by:
#   1. Detecting OS / architecture.
#   2. Verifying prerequisites (git + clang++/g++).
#   3. Cloning the official repository.
#   4. Building with `make`.
#   5. Running the test suite to confirm the build works.
#   6. Installing the `novis` binary to /usr/local/bin or ~/.local/bin.
#   7. Making sure the install location is on PATH (for bash and zsh).
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/lobami/novis/main/install.sh | bash
#   curl -fsSL https://raw.githubusercontent.com/lobami/novis/main/install.sh | bash -s -- --uninstall
#
set -euo pipefail

REPO_URL="https://github.com/lobami/novis.git"
REPO_BRANCH="main"
BIN_NAME="novis"

# ------------------------------------------------------------------------------
# Logging helpers
# ------------------------------------------------------------------------------
log()   { printf '\033[1;36m[novis]\033[0m %s\n' "$*"; }
ok()    { printf '\033[1;32m[✓ ok]\033[0m %s\n' "$*"; }
warn()  { printf '\033[1;33m[warn]\033[0m %s\n' "$*"; }
fail()  { printf '\033[1;31m[✗ fail]\033[0m %s\n' "$*" >&2; exit 1; }
hr()    { printf -- '------------------------------------------------------------\n'; }

# ------------------------------------------------------------------------------
# Argument parsing
# ------------------------------------------------------------------------------
ACTION="install"
for arg in "$@"; do
    case "$arg" in
        --uninstall) ACTION="uninstall" ;;
        --help|-h)
            cat <<'USAGE'
novis installer

usage:
  bash install.sh              # install
  bash install.sh --uninstall   # remove the novis toolchain
  bash install.sh --help        # this help

docs: https://github.com/lobami/novis
USAGE
            exit 0
            ;;
        *) fail "unknown argument: $arg" ;;
    esac
done

# ------------------------------------------------------------------------------
# Platform detection
# ------------------------------------------------------------------------------
OS="$(uname -s)"
ARCH="$(uname -m)"

case "$OS" in
    Linux|Darwin) ;;
    *) fail "unsupported OS: $OS. Novis installer currently supports macOS and Linux." ;;
esac

case "$ARCH" in
    arm64|x86_64|amd64) ;;
    *) fail "unsupported architecture: $ARCH" ;;
esac

log "Detected platform: $OS / $ARCH"

# ------------------------------------------------------------------------------
# Locate existing install (for uninstall / upgrade)
# ------------------------------------------------------------------------------
detect_install_dir() {
    if [ -w "/usr/local/bin" ] && [ -d "/usr/local/bin" ]; then
        echo "/usr/local/bin"
    else
        echo "$HOME/.local/bin"
    fi
}

INSTALL_DIR="$(detect_install_dir)"
mkdir -p "$INSTALL_DIR"

# ------------------------------------------------------------------------------
# Uninstall path
# ------------------------------------------------------------------------------
if [ "$ACTION" = "uninstall" ]; then
    log "Uninstalling Novis from $INSTALL_DIR"
    if [ -f "$INSTALL_DIR/$BIN_NAME" ]; then
        rm -f "$INSTALL_DIR/$BIN_NAME"
        ok "Removed $INSTALL_DIR/$BIN_NAME"
    else
      warn "$BIN_NAME not found in $INSTALL_DIR (nothing to remove)"
    fi
    log "Uninstall complete."
    exit 0
fi

# ------------------------------------------------------------------------------
# Prerequisite check
# ------------------------------------------------------------------------------
hr
log "Checking prerequisites"
hr

if ! command -v git >/dev/null 2>&1; then
    fail "git is required. Install it with your package manager (brew install git / apt-get install git)."
fi
ok "git: $(command -v git)"

CXX=""
if command -v clang++ >/dev/null 2>&1; then
    CXX="clang++"
elif command -v g++ >/dev/null 2>&1; then
    CXX="g++"
fi

if [ -z "$CXX" ]; then
    fail "no C++ compiler found. Install clang++ (preferred) or g++ that supports C++17."
fi
ok "C++ compiler: $CXX ($(command -v "$CXX"))"

# ------------------------------------------------------------------------------
# Download source
# ------------------------------------------------------------------------------
hr
log "Cloning Novis from $REPO_URL (branch: $REPO_BRANCH)"
hr

TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TEMP_DIR"' EXIT
cd "$TEMP_DIR"

if ! git clone --depth 1 --branch "$REPO_BRANCH" "$REPO_URL" novis-src; then
    fail "failed to clone $REPO_URL. Check your network and try again."
fi
cd novis-src
ok "Source cloned to $TEMP_DIR/novis-src"

# ------------------------------------------------------------------------------
# Build
# ------------------------------------------------------------------------------
hr
log "Building Novis with $CXX"
hr

if ! make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"; then
    fail "build failed. See the output above for the compiler error."
fi
ok "Build complete"

if [ ! -x "./$BIN_NAME" ]; then
    fail "build finished but ./$BIN_NAME is missing or not executable."
fi

# ------------------------------------------------------------------------------
# Sanity test
# ------------------------------------------------------------------------------
hr
log "Running the test suite (make test) to verify the binary"
hr

if ! make test >/dev/null 2>&1; then
    fail "the test suite failed. The binary was built but does not pass its own tests. Aborting install."
fi
ok "Test suite green"

# ------------------------------------------------------------------------------
# Install binary
# ------------------------------------------------------------------------------
hr
log "Installing $BIN_NAME to $INSTALL_DIR"
hr

  install -m 0755 "./$BIN_NAME" "$INSTALL_DIR/$BIN_NAME"
ok "Installed $INSTALL_DIR/$BIN_NAME"

# ------------------------------------------------------------------------------
# PATH management (bash and zsh)
# ------------------------------------------------------------------------------
ensure_path_line() {
    local rcfile="$1"
    local export_line="export PATH=\"\$PATH:$INSTALL_DIR\""

    if [ ! -f "$rcfile" ]; then
        return 1
    fi
    if grep -Fq "$INSTALL_DIR" "$rcfile"; then
        return 1
    fi
    printf '\n# Added by Novis installer\n%s\n' "$export_line" >> "$rcfile"
    return 0
}

if [[ ":$PATH:" != *":$INSTALL_DIR:"* ]]; then
    case ":$SHELL:" in
        *bash) rc="$HOME/.bashrc" ;;
        *zsh)  rc="$HOME/.zshrc" ;;
        *)     rc="" ;;
    esac

    if [ -n "${rc:-}" ]; then
        if ensure_path_line "$rc"; then
            ok "Added $INSTALL_DIR to PATH in $rc"
            warn "Open a new shell or run: source $rc"
        else
            warn "$INSTALL_DIR not on PATH. Add it manually: export PATH=\"\$PATH:$INSTALL_DIR\""
        fi
    else
        warn "$INSTALL_DIR not on PATH. Add it manually: export PATH=\"\$PATH:$INSTALL_DIR\""
    fi
else
    ok "$INSTALL_DIR is already on PATH"
fi

# ------------------------------------------------------------------------------
# Final report
# ------------------------------------------------------------------------------
hr
log "Novis installed successfully 🚀"
hr
cat <<EOF
  version   : $("$INSTALL_DIR/$BIN_NAME" --help 2>/dev/null | head -n 1 || echo "unknown")
  binary    : $INSTALL_DIR/$BIN_NAME
  repo      : $REPO_URL
  python    : not required (Novis is native, no CPython)
  help      : $BIN_NAME --help
  examples  : $BIN_NAME run examples/bank_risk.novis
  tests     : $BIN_NAME check <file.novis>

Quick start:
  $BIN_NAME                              # REPL
  $BIN_NAME env init                     # create .novis project environment
  $BIN_NAME importpy pandas             # install native provider facade

Created by Loth Mejía Martínez · México · 2026
EOF
