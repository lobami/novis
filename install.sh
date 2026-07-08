#!/usr/bin/env bash
# ==============================================================================
# Novis Universal Installer
# https://github.com/lobami/novis
# Created by Loth Mejía Martínez · México · 2026
# ==============================================================================
# Installs or upgrades the Novis toolchain by:
#   1. Detecting OS / architecture.
#   2. Verifying prerequisites (git + clang++/g++).
#   3. Cloning the official repository at the requested branch.
#   4. Detecting the version of any existing install.
#   5. Building the new version with `make`.
#   6. Running the test suite to confirm the build works.
#   7. Replacing the binary in /usr/local/bin (or ~/.local/bin).
#   8. Making sure the install location is on PATH (for bash and zsh).
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/lobami/novis/main/install.sh | bash
#   curl -fsSL https://raw.githubusercontent.com/lobami/novis/main/install.sh | bash -s -- --uninstall
#   curl -fsSL https://raw.githubusercontent.com/lobami/novis/main/install.sh | bash -s -- --force
#   curl -fsSL https://raw.githubusercontent.com/lobami/novis/main/install.sh | bash -s -- --channel dev
#
set -euo pipefail

REPO_URL="https://github.com/lobami/novis.git"
REPO_BRANCH="main"
BIN_NAME="novis"
INSTALLER_VERSION="1.1.0"

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
FORCE="0"
for arg in "$@"; do
    case "$arg" in
        --uninstall) ACTION="uninstall" ;;
        --force|-f)  FORCE="1" ;;
        --channel)   shift; REPO_BRANCH="${1:-main}" ;;
        --channel=*) REPO_BRANCH="${arg#--channel=}" ;;
        --help|-h)
            cat <<'USAGE'
novis installer

usage:
  bash install.sh                       # install or upgrade
  bash install.sh --force                # reinstall even when versions match
  bash install.sh --channel <branch>     # install a different branch (dev, etc)
  bash install.sh --uninstall            # remove the novis toolchain
  bash install.sh --help                 # this help

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
# Version helpers
# ------------------------------------------------------------------------------
# Extract the leading "X.Y.Z" from a string like "Novis 1.0.0".
extract_version() {
    local text="$1"
    printf '%s\n' "$text" | grep -Eo '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n 1
}

# read_local_version: prints the installed version, or empty if not installed.
read_local_version() {
    local bin="$INSTALL_DIR/$BIN_NAME"
    if [ -x "$bin" ]; then
        local header
        header="$("$bin" --help 2>/dev/null | head -n 1 || true)"
        extract_version "$header"
    fi
}

# read_remote_version: clones (shallow) and reads the version from the help
# line of the freshly built binary. Heavy but reliable; it does not depend on
# tags, only on the source.
read_remote_version() {
    local src_dir="$1"
    local raw
    raw="$(grep -Eo 'NOVIS_VERSION[[:space:]]*=[[:space:]]*"[0-9]+\.[0-9]+(\.[0-9]+)?"' \
        "$src_dir/src/main.cpp" | head -n 1)"
    if [ -z "$raw" ]; then
        raw="$(grep -Eo 'const char\*[[:space:]]+NOVIS_VERSION[[:space:]]*=[[:space:]]*"[0-9]+\.[0-9]+(\.[0-9]+)?"' \
            "$src_dir/src/main.cpp" | head -n 1)"
    fi
    if [ -z "$raw" ]; then
        printf '%s\n' ""
        return
    fi
    printf '%s\n' "$raw" | grep -Eo '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n 1
}

# semver_cmp a b -> prints -1, 0 or 1
semver_cmp() {
    local a="$1" b="$2"
    local IFS=.
    local i a1=() b1=()
    read -r -a a1 <<< "$a"
    read -r -a b1 <<< "$b"
    for i in 0 1 2; do
        local x="${a1[i]:-0}"
        local y="${b1[i]:-0}"
        if [ "$x" -lt "$y" ]; then printf '%s\n' -1; return; fi
        if [ "$x" -gt "$y" ]; then printf '%s\n'  1; return; fi
    done
    printf '%s\n' 0
}

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
# Show current install state, if any
# ------------------------------------------------------------------------------
LOCAL_VERSION="$(read_local_version || true)"

if [ -n "$LOCAL_VERSION" ]; then
    log "Existing install detected: $LOCAL_VERSION at $INSTALL_DIR/$BIN_NAME"
else
    log "No existing install found in $INSTALL_DIR"
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

REMOTE_VERSION="$(read_remote_version "$TEMP_DIR/novis-src" || true)"
if [ -n "$REMOTE_VERSION" ]; then
    log "Latest available: $REMOTE_VERSION"
else
    warn "Could not read latest version from source. Continuing."
fi

# ------------------------------------------------------------------------------
# Decide whether we need to rebuild
# ------------------------------------------------------------------------------
NEED_BUILD="1"
if [ -n "$LOCAL_VERSION" ] && [ -n "$REMOTE_VERSION" ]; then
    cmp="$(semver_cmp "$REMOTE_VERSION" "$LOCAL_VERSION")"
    case "$cmp" in
        -1)
            log "Local install is newer than remote ($LOCAL_VERSION > $REMOTE_VERSION). Skipping install."
            log "Use --force if you really want to reinstall."
            exit 0
            ;;
        0)
            if [ "$FORCE" != "1" ]; then
                log "Local install already matches latest ($LOCAL_VERSION). Nothing to do."
                log "Use --force to reinstall."
                exit 0
            fi
            log "Local matches latest; --force requested, reinstalling."
            ;;
        1) log "Upgrade available: $LOCAL_VERSION -> $REMOTE_VERSION" ;;
    esac
fi

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
# Install / replace binary
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
FINAL_VERSION="$(read_local_version || true)"
hr
log "Novis installed successfully 🚀"
hr
cat <<EOF
  installer : $INSTALLER_VERSION
  version   : ${FINAL_VERSION:-unknown}
  binary    : $INSTALL_DIR/$BIN_NAME
  repo      : $REPO_URL
  branch    : $REPO_BRANCH
  python    : not required (Novis is native, no CPython)
  help      : $BIN_NAME --help
  examples  : $BIN_NAME run examples/bank_risk.novis
  tests     : $BIN_NAME check <file.novis>
  upgrade   : rerun the same curl command, or pass --force to reinstall
  uninstall : curl ... | bash -s -- --uninstall

Quick start:
  $BIN_NAME                              # REPL
  $BIN_NAME env init                     # create .novis project environment
  $BIN_NAME importpy pandas             # install native provider facade

Created by Loth Mejía Martínez · México · 2026
EOF
