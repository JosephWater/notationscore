#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$project_dir/scripts/env.sh"
cd "$project_dir"
mkdir -p artifacts
for preset in linux-debug linux-release; do
    cmake --preset "$preset"
    cmake --build --preset "$preset" --parallel 6
    ctest --preset "$preset"
done
QT_QPA_PLATFORM=offscreen build/linux-debug/jianpu_lab examples/ensemble.musicxml --render artifacts/jianpu-preview.png
QT_QPA_PLATFORM=offscreen gdb -q -batch \
    -ex 'set pagination off' -ex 'set debuginfod enabled off' \
    -ex 'break jianpu::ScoreCanvas::paintEvent' -ex 'run --smoke' -ex 'bt 4' \
    -ex 'disable 1' -ex 'continue' --args build/linux-debug/jianpu_lab > artifacts/gdb-smoke.log 2>&1
rg 'Breakpoint 1, jianpu::ScoreCanvas::paintEvent' artifacts/gdb-smoke.log
rg 'exited normally' artifacts/gdb-smoke.log
"$project_dir/scripts/check-environment.sh"
