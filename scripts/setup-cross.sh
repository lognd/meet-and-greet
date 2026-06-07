#!/usr/bin/env bash
# setup-cross.sh  --  install every cross-compiler needed for `make release`
#
# Host:    Ubuntu 22.04 arm64 (Snapdragon X Elite / WSL2)
# Targets: Linux arm64 (native), Linux x86-64, Windows x86-64, Windows arm64
#
# Usage:
#   chmod +x scripts/setup-cross.sh
#   sudo scripts/setup-cross.sh
#
# Re-running this script is safe; it skips steps that are already done.

set -euo pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'; NC='\033[0m'
info()  { echo -e "${CYAN}==> $*${NC}"; }
ok()    { echo -e "${GREEN}    ok: $*${NC}"; }
die()   { echo -e "${RED}ERROR: $*${NC}" >&2; exit 1; }

[[ $EUID -eq 0 ]] || die "Run this script with sudo."

# --------------------------------------------------------------------------
# 1. apt packages
# --------------------------------------------------------------------------

info "Installing apt cross-compiler packages..."

apt-get update -qq

apt-get install -y \
    g++-x86-64-linux-gnu \
    gcc-x86-64-linux-gnu \
    binutils-x86-64-linux-gnu \
    g++-mingw-w64-x86-64-posix \
    gcc-mingw-w64-x86-64-posix \
    binutils-mingw-w64-x86-64 \
    mingw-w64-x86-64-dev \
    curl \
    xz-utils

ok "apt packages installed"

# --------------------------------------------------------------------------
# 2. llvm-mingw  (Windows arm64 target -- not in Ubuntu apt)
# --------------------------------------------------------------------------

LLVM_MINGW_PREFIX="/opt/llvm-mingw"
LLVM_MINGW_BIN="$LLVM_MINGW_PREFIX/bin"

if command -v "$LLVM_MINGW_BIN/aarch64-w64-mingw32-clang++" &>/dev/null; then
    ok "llvm-mingw already installed at $LLVM_MINGW_PREFIX"
else
    info "Fetching latest llvm-mingw release for arm64..."

    RELEASE=$(curl -sf \
        "https://api.github.com/repos/mstorsjo/llvm-mingw/releases/latest" \
        | python3 -c \
          "import sys,json; r=json.load(sys.stdin); \
           assets=[a for a in r['assets'] if 'ubuntu-22.04-aarch64' in a['name'] and 'ucrt' in a['name']]; \
           print(assets[0]['browser_download_url'])")

    [[ -n "$RELEASE" ]] || die "Could not find llvm-mingw download URL"

    TARBALL="/tmp/llvm-mingw.tar.xz"
    info "Downloading $RELEASE ..."
    curl -fL "$RELEASE" -o "$TARBALL"

    info "Extracting to $LLVM_MINGW_PREFIX ..."
    rm -rf "$LLVM_MINGW_PREFIX"
    mkdir -p /opt
    tar -xf "$TARBALL" -C /opt
    # the tarball unpacks to a versioned directory; rename it
    EXTRACTED=$(find /opt -maxdepth 1 -name 'llvm-mingw-*' -type d | head -1)
    [[ -n "$EXTRACTED" ]] || die "Could not find extracted llvm-mingw directory"
    mv "$EXTRACTED" "$LLVM_MINGW_PREFIX"
    rm -f "$TARBALL"

    ok "llvm-mingw installed at $LLVM_MINGW_PREFIX"
fi

# --------------------------------------------------------------------------
# 3. Verify
# --------------------------------------------------------------------------

info "Verifying toolchains..."

check() {
    local bin="$1" label="$2"
    if command -v "$bin" &>/dev/null; then
        ok "$label  ($bin)"
    else
        echo -e "${RED}    MISSING: $label  ($bin)${NC}"
    fi
}

check aarch64-linux-gnu-g++           "Linux arm64   (native cross-compiler)"
check x86_64-linux-gnu-g++            "Linux x86-64  (cross-compiler)"
check x86_64-w64-mingw32-g++-posix    "Windows x86-64 (MinGW-w64 posix)"
check "$LLVM_MINGW_BIN/aarch64-w64-mingw32-clang++" \
                                       "Windows arm64  (llvm-mingw)"

echo ""
info "All done.  You can now run:"
echo "    make release"
echo "    make release XORKEY=\"\$(grep encryption_key app.toml | cut -d'\"' -f2)\""
