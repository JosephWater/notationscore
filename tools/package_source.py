#!/usr/bin/env python3
"""Package corresponding application and pinned core sources, without build output."""
from pathlib import Path
import argparse
import hashlib
import io
import tarfile

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, default=root / "artifacts/JianpuLab-source.tar.gz")
args = parser.parse_args()
core = root / "references/MuseScore"
if not (core / "muse/framework/global/globalmodule.cpp").is_file():
    parser.error("Fetch the complete pinned MuseScore source and submodules first")
excluded = {".git", "build", "artifacts", ".cache", "__pycache__"}


def source_filter(info: tarfile.TarInfo):
    relative = Path(info.name).parts[1:]
    if (relative and relative[0] in excluded) or any(part in {".git", "__pycache__"} for part in relative):
        return None
    info.uid = info.gid = 0
    info.uname = info.gname = ""
    return info


args.output.parent.mkdir(parents=True, exist_ok=True)
with tarfile.open(args.output, "w:gz", compresslevel=6) as archive:
    archive.add(root, arcname="JianpuLab", filter=source_filter)
    lock = b"4509c9072c583f7ab0c805ded2ab88f29e0ec0e5\n42b02245d36f1f37dd2b04a382e27c28c9ece006\na9db17d2f0478859711873f2db156eedfc082040\n"
    info = tarfile.TarInfo("JianpuLab/references/MuseScore/JIANPU_SOURCE_LOCK")
    info.size = len(lock)
    archive.addfile(info, io.BytesIO(lock))
with args.output.open("rb") as source:
    checksum = hashlib.file_digest(source, "sha256").hexdigest()
args.output.with_suffix(args.output.suffix + ".sha256").write_text(
    f"{checksum}  {args.output.name}\n", encoding="utf-8"
)
print(f"Corresponding application and pinned core sources: {args.output}")
