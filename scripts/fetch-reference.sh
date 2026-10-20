#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
reference_dir="$project_dir/references/MuseScore"
revision=4509c9072c583f7ab0c805ded2ab88f29e0ec0e5
mkdir -p "$project_dir/references" "$project_dir/artifacts"
if [[ -f "$reference_dir/JIANPU_SOURCE_LOCK" && ! -e "$reference_dir/.git" ]]; then
    mapfile -t archive_revisions < "$reference_dir/JIANPU_SOURCE_LOCK"
    [[ "${archive_revisions[*]}" == "$revision 42b02245d36f1f37dd2b04a382e27c28c9ece006 a9db17d2f0478859711873f2db156eedfc082040" ]] || {
        echo 'Source archive revision manifest differs.' >&2
        exit 1
    }
    [[ -f "$reference_dir/muse/framework/global/globalmodule.cpp" && -f "$reference_dir/muse_deps/buildtools/manifest.cmake" ]]
    echo 'Using the complete pinned core from the corresponding-source archive.'
    exit 0
fi
if [[ ! -d "$reference_dir" ]]; then
    git clone --depth 1 --no-checkout https://github.com/musescore/MuseScore.git "$reference_dir"
    git -C "$reference_dir" fetch --depth 1 origin "$revision"
    git -C "$reference_dir" checkout --detach "$revision"
fi
[[ "$(git -C "$reference_dir" rev-parse HEAD)" == "$revision" ]] || {
    echo 'Reference revision differs. Existing checkout preserved; use a separate directory for another revision.' >&2
    exit 1
}
git -C "$reference_dir" submodule update --init --recursive --depth 1
git -C "$reference_dir" rev-parse HEAD > "$project_dir/artifacts/musescore-revisions.txt"
git -C "$reference_dir" submodule status --recursive >> "$project_dir/artifacts/musescore-revisions.txt"
cat "$project_dir/artifacts/musescore-revisions.txt"
