#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$project_dir/scripts/env.sh"
cmake --version
ninja --version
git --version
g++ --version | head -1
gdb --version | head -1
clang-format --version
"${HOME}/Qt/6.10.2/gcc_64/bin/qmake" -query QT_VERSION
qt_dir="${HOME}/Qt/6.10.2/gcc_64"
for plugin in platforms/libqoffscreen.so platforms/libqxcb.so imageformats/libqsvg.so multimedia/libffmpegmediaplugin.so; do
    output="$(ldd "$qt_dir/plugins/$plugin")"
    if [[ "$output" == *'not found'* ]]; then
        echo "$output" >&2
        exit 1
    fi
    echo "Dependencies OK: $plugin"
done
python3 "$project_dir/scripts/record-environment.py"
