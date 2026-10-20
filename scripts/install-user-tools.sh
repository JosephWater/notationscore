#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
tools_dir="${HOME}/.local/share/jianpu-tools/venv"
cache_dir="${HOME}/.cache/jianpu-lab"
sysroot_dir="${HOME}/.local/opt/jianpu-sysroot"
[[ "$(uname -s)" == Linux && "$(uname -m)" == x86_64 ]] || {
    echo 'This installer targets Ubuntu x86_64. See docs/windows.md for Windows.' >&2
    exit 1
}
for command_name in curl git g++ gdb apt-get dpkg-deb python3; do
    command -v "$command_name" >/dev/null || { echo "Missing: $command_name" >&2; exit 1; }
done
mkdir -p "${HOME}/.local/bin" "$cache_dir/debs" "$sysroot_dir" "$project_dir/artifacts"
if [[ ! -x "${HOME}/.local/bin/uv" ]]; then
    curl --fail --location --retry 3 https://astral.sh/uv/0.13.1/install.sh -o "$cache_dir/uv-installer.sh"
    UV_NO_MODIFY_PATH=1 UV_INSTALL_DIR="${HOME}/.local/bin" sh "$cache_dir/uv-installer.sh"
fi
uv_bin="${HOME}/.local/bin/uv"
if [[ ! -x "$tools_dir/bin/python" ]]; then
    "$uv_bin" venv --python 3.12.15 "$tools_dir"
fi
"$uv_bin" pip sync --python "$tools_dir/bin/python" "$project_dir/scripts/tools-requirements.lock"
for command_name in cmake ctest cpack ninja aqt clang-format; do
    destination="${HOME}/.local/bin/$command_name"
    if [[ -e "$destination" && ! -L "$destination" ]]; then
        echo "Existing file blocks tool link: $destination" >&2
        exit 1
    fi
    ln -sfn "$tools_dir/bin/$command_name" "$destination"
done

# Download and extract only; no dpkg installation or privileged package scripts run.
mapfile -t packages < "$project_dir/scripts/apt-packages.lock"
(
    cd "$cache_dir/debs"
    apt-get download "${packages[@]}"
)
for package in "${packages[@]}"; do
    package_name="${package%%=*}"
    for deb in "$cache_dir/debs/${package_name}_"*.deb; do
        dpkg-deb -x "$deb" "$sysroot_dir"
    done
done

qt_dir="${HOME}/Qt/6.10.2/gcc_64"
qt_stamp="${HOME}/Qt/6.10.2/.jianpu-sdk-complete"
if [[ ! -f "$qt_stamp" ]]; then
    "$tools_dir/bin/python" -m aqt install-qt linux desktop 6.10.2 linux_gcc_64 \
        -O "${HOME}/Qt" -m qt5compat qtnetworkauth qtmultimedia qtshadertools
    for qt_component in Widgets Qml Quick QuickControls2 Xml Svg PrintSupport Multimedia Test LinguistTools; do
        [[ -f "$qt_dir/lib/cmake/Qt6$qt_component/Qt6${qt_component}Config.cmake" ]]
    done
    touch "$qt_stamp"
fi

mkdir -p "${HOME}/.config/jianpu-lab"
install -m 644 "$project_dir/scripts/env.sh" "${HOME}/.config/jianpu-lab/env.sh"
python3 - <<'PY'
from pathlib import Path
import shutil
home = Path.home()
bashrc = home / '.bashrc'
text = bashrc.read_text() if bashrc.exists() else ''
marker = '# >>> jianpu-lab user tools >>>'
if marker not in text:
    backup = home / '.config/jianpu-lab/bashrc.before-jianpu'
    if bashrc.exists() and not backup.exists():
        shutil.copy2(bashrc, backup)
    bashrc.write_text(text + '\n' + marker + '\n'
        '[ ! -f "$HOME/.config/jianpu-lab/env.sh" ] || . "$HOME/.config/jianpu-lab/env.sh"\n'
        '# <<< jianpu-lab user tools <<<\n')
PY
"$project_dir/scripts/install-vscode.sh"
python3 "$project_dir/scripts/record-environment.py"
echo 'User toolchain ready. Run: source scripts/env.sh'
