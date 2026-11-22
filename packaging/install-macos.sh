#!/bin/bash
# Installs flux_filter (VST3, AU and the standalone app) on macOS.
#
# Two ways to run it:
#   1. From GitHub, downloads the latest release (nothing to unblock):
#        curl -fsSL https://raw.githubusercontent.com/tilohk/flux_filter/main/packaging/install-macos.sh | bash
#   2. From an unpacked release zip: open Terminal, type "bash ", drag this file into the
#      window and press Enter. It installs the files next to it and unblocks them.
#
# The plug-ins go into your user plug-in folders (no administrator password needed):
#   ~/Library/Audio/Plug-Ins/VST3/flux_filter.vst3
#   ~/Library/Audio/Plug-Ins/Components/flux_filter.component
set -euo pipefail

REPO="tilohk/flux_filter"
VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"

say() { printf '%s\n' "$*"; }
fail() {
    printf 'Error: %s\n' "$*" >&2
    exit 1
}

[ "$(uname -s)" = "Darwin" ] || fail "this installer is for macOS."

if pgrep -x Live > /dev/null 2>&1; then
    say "Ableton Live is running. Please quit Live first, then run the installer again."
    exit 1
fi

# --- find the files: next to this script, or download the latest release -------------------
SCRIPT_DIR=""
if [ -n "${BASH_SOURCE[0]:-}" ] && [ -f "${BASH_SOURCE[0]}" ]; then
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
fi

WORK=""
cleanup() {
    if [ -n "$WORK" ]; then rm -rf "$WORK"; fi
}
trap cleanup EXIT

if [ -n "$SCRIPT_DIR" ] && [ -d "$SCRIPT_DIR/flux_filter.vst3" ]; then
    SOURCE="$SCRIPT_DIR"
    say "Installing flux_filter from $SOURCE"
else
    WORK="$(mktemp -d)"
    say "Looking up the latest flux_filter release..."
    URL="$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" |
        grep -o '"browser_download_url": *"[^"]*-macOS\.zip"' | head -n 1 | sed 's/.*"\(https[^"]*\)"$/\1/')"
    [ -n "$URL" ] || fail "could not find a macOS download in the latest release of $REPO."
    say "Downloading $(basename "$URL")..."
    curl -fsSL "$URL" -o "$WORK/flux_filter.zip" || fail "download failed."
    ditto -x -k "$WORK/flux_filter.zip" "$WORK" || fail "could not unpack the download."
    SOURCE="$(dirname "$(find "$WORK" -maxdepth 2 -name flux_filter.vst3 -type d | head -n 1)")"
    [ -d "$SOURCE/flux_filter.vst3" ] || fail "the download does not contain flux_filter.vst3."
fi

[ -d "$SOURCE/flux_filter.component" ] || fail "flux_filter.component is missing in $SOURCE."

# --- install ------------------------------------------------------------------------------
mkdir -p "$VST3_DIR" "$AU_DIR"
rm -rf "$VST3_DIR/flux_filter.vst3" "$AU_DIR/flux_filter.component"
cp -R "$SOURCE/flux_filter.vst3" "$VST3_DIR/"
cp -R "$SOURCE/flux_filter.component" "$AU_DIR/"
xattr -dr com.apple.quarantine "$VST3_DIR/flux_filter.vst3" "$AU_DIR/flux_filter.component" 2> /dev/null || true
say "Installed: $VST3_DIR/flux_filter.vst3"
say "Installed: $AU_DIR/flux_filter.component"

if [ -d "$SOURCE/flux_filter.app" ]; then
    APP_DIR="${FLUX_FILTER_APP_DIR:-/Applications}" # override only for testing
    [ -w "$APP_DIR" ] || APP_DIR="$HOME/Applications"
    mkdir -p "$APP_DIR"
    rm -rf "$APP_DIR/flux_filter.app"
    cp -R "$SOURCE/flux_filter.app" "$APP_DIR/"
    xattr -dr com.apple.quarantine "$APP_DIR/flux_filter.app" 2> /dev/null || true
    say "Installed: $APP_DIR/flux_filter.app"
fi

# Ask macOS to re-register the Audio Unit right away.
killall -9 AudioComponentRegistrar > /dev/null 2>&1 || true

# --- copies in the system-wide folders would be found twice (and may still be blocked) ----
SYSTEM_COPIES=()
for p in /Library/Audio/Plug-Ins/VST3/flux_filter.vst3 /Library/Audio/Plug-Ins/Components/flux_filter.component; do
    [ -e "$p" ] && SYSTEM_COPIES+=("$p")
done
if [ ${#SYSTEM_COPIES[@]} -gt 0 ]; then
    say ""
    say "Note: there is also a copy in the system-wide plug-in folder. Remove it with"
    say "(asks for your Mac password):"
    say "  sudo rm -rf ${SYSTEM_COPIES[*]}"
fi

say ""
say "Done. Start your DAW and rescan plug-ins."
say "Ableton Live: Settings > Plug-Ins, hold Option and click Rescan"
say "(Option also retries plug-ins that failed to load before)."
