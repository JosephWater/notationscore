#!/usr/bin/env bash
set -euo pipefail
vscode_cli=""
if command -v code >/dev/null 2>&1; then
    vscode_cli="$(command -v code)"
else
    shopt -s nullglob
    for candidate in "${HOME}"/.vscode-server/cli/servers/Stable-*/server/bin/remote-cli/code; do
        if [[ -z "$vscode_cli" || "$candidate" -nt "$vscode_cli" ]]; then vscode_cli="$candidate"; fi
    done
fi
if [[ -n "$vscode_cli" && -n "${VSCODE_IPC_HOOK_CLI:-}" ]]; then
    "$vscode_cli" --install-extension ms-vscode.cpptools --install-extension ms-vscode.cmake-tools
else
    echo 'No live VS Code CLI connection. Install extensions listed in .vscode/extensions.json on reconnect.'
fi
# A remote-machine setting lets Windows keep its own cmake executable path.
python3 - <<'PY'
from pathlib import Path
import json
import shutil
path = Path.home() / '.vscode-server/data/Machine/settings.json'
path.parent.mkdir(parents=True, exist_ok=True)
try:
    settings = json.loads(path.read_text()) if path.exists() else {}
except json.JSONDecodeError:
    print('Remote settings use JSONC. Set cmake.cmakePath to ~/.local/bin/cmake manually; existing settings preserved.')
else:
    if path.exists():
        backup = path.with_suffix('.before-jianpu.json')
        if not backup.exists():
            shutil.copy2(path, backup)
    settings['cmake.cmakePath'] = str(Path.home() / '.local/bin/cmake')
    path.write_text(json.dumps(settings, ensure_ascii=False, indent=2) + '\n')
PY
