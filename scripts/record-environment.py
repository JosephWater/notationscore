#!/usr/bin/env python3
"""Record versions, sources and package hashes without collecting user settings or secrets."""
from datetime import datetime
from pathlib import Path
from zoneinfo import ZoneInfo
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parent.parent
HOME = Path.home()


def run(*args):
    result = subprocess.run(args, capture_output=True, text=True, check=False)
    return {"exit_code": result.returncode, "output": result.stdout.strip()}


manifest = {
    "recorded_at": datetime.now(ZoneInfo("Asia/Shanghai")).isoformat(),
    "platform": run("uname", "-srm"),
    "tools": {tool: run(str(HOME / ".local/bin" / tool), "--version")
              for tool in ("uv", "cmake", "ninja", "clang-format")},
    "git": run("git", "--version"),
    "compiler": run("g++", "--version"),
    "debugger": run("gdb", "--version"),
    "qt": run(str(HOME / "Qt/6.10.2/gcc_64/bin/qmake"), "-query"),
    "python_packages": run(str(HOME / ".local/bin/uv"), "pip", "freeze", "--python",
                           str(HOME / ".local/share/jianpu-tools/venv/bin/python")),
    "reference": run("git", "-C", str(ROOT / "references/MuseScore"), "rev-parse", "HEAD"),
    "submodules": run("git", "-C", str(ROOT / "references/MuseScore"), "submodule", "status", "--recursive"),
    "extension_versions": sorted(p.name for p in (HOME / ".vscode-server/extensions").glob("ms-vscode.*")),
    "sources": {
        "uv": "https://astral.sh/uv/0.13.1/install.sh",
        "python": "uv-managed CPython 3.12.15 (python-build-standalone)",
        "python_packages": "https://pypi.org/simple/ (scripts/tools-requirements.lock)",
        "qt": "https://download.qt.io/online/qtsdkrepository/ (aqtinstall; Qt 6.10.2 linux_gcc_64)",
        "opengl": "Ubuntu resolute apt sources (scripts/apt-packages.lock)",
        "musescore": "https://github.com/musescore/MuseScore.git",
        "extensions": "Visual Studio Marketplace via VS Code Remote CLI",
    },
    "deb_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                   for p in sorted((HOME / ".cache/jianpu-lab/debs").glob("*.deb"))},
}
(ROOT / "artifacts").mkdir(exist_ok=True)
target = ROOT / "artifacts/environment.json"
target.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
print(target)
