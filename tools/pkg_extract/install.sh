#!/usr/bin/env bash
# Game folder from fake-signed backup packages (what some PS4 dump tools write):
#   bash tools/pkg_extract/install.sh <game.pkg> [<update.pkg>] [<destination folder, default roms>]
# The game goes to <destination>/CUSA03173, the update is unpacked next to it and copied over it,
# then the result is checked as the launcher does (Bloodborne 1.09). Windows: windows\install-pkg.cmd.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
if (( $# < 1 || $# > 3 )); then
    echo 'usage: install.sh <game.pkg> [<update.pkg>] [<destination folder>]' >&2; exit 2
fi
game_pkg=$1 update_pkg=${2:-} dest=${3:-roms}
tool=out/pkg-extract
if [[ $OSTYPE == msys* || $OSTYPE == cygwin* ]]; then tool=out/pkg-extract.exe; fi
if [[ ! -x $tool ]]; then
    cmake -S tools/pkg_extract -B out/pkg_extract -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
    ninja -C out/pkg_extract >/dev/null
fi
mkdir -p "$dest"
game=$dest/CUSA03173
if [[ -e $game/eboot.bin ]]; then
    echo "$game already holds a game; move it away first." >&2; exit 1
fi
"$tool" "$game_pkg" "$game"
if [[ -n $update_pkg ]]; then
    update=$dest/CUSA03173-UPDATE
    rm -rf "$update"
    "$tool" "$update_pkg" "$update"
    echo "Copying the update over the game"
    cp -rf "$update"/. "$game"/
    rm -rf "$update"
fi
PYTHON=${PYTHON:-$(command -v python3)}
"$PYTHON" - "$game" <<'EOF'
import sys
sys.path.insert(0, 'scripts')
import game_check
issue = game_check.problem(sys.argv[1])
if issue:
    print('Game check:', game_check.explain(*issue))
    sys.exit(1)
print(f'Game check: Bloodborne 1.09 in {sys.argv[1]}, ready')
EOF
