#!/bin/sh
# GGA: Recompiled - adds the game to the applications menu, with its icon (Linux).
# Run it once from the extracted folder:  sh install-desktop-entry.sh
# Run it again if you move the folder. To remove: delete ~/.local/share/applications/ggarecompiled.desktop
# and ~/.local/share/icons/ggarecompiled.png
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
if [ ! -x "$DIR/GGARecompiled" ] || [ ! -f "$DIR/icon.png" ]; then
    echo "GGARecompiled or icon.png not found next to this script: run it from the extracted folder." >&2
    exit 1
fi

APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
ICONS="${XDG_DATA_HOME:-$HOME/.local/share}/icons"
mkdir -p "$APPS" "$ICONS"
cp "$DIR/icon.png" "$ICONS/ggarecompiled.png"

# Path= : the game must start from its own folder (it reads assets/ there).
cat > "$APPS/ggarecompiled.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=GGA: Recompiled
Comment=Goemon's Great Adventure, natively recompiled
Exec="$DIR/GGARecompiled"
Path=$DIR
Icon=$ICONS/ggarecompiled.png
Terminal=false
Categories=Game;
StartupWMClass=GGARecompiled
EOF
chmod +x "$APPS/ggarecompiled.desktop"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$APPS" >/dev/null 2>&1 || true
fi
echo "Done: GGA: Recompiled is now in your applications menu."
