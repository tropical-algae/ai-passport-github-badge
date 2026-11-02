#!/usr/bin/env bash
# Render the firmware's real LVGL C views into PPM files, with synthetic data.
set -euo pipefail
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
output=${1:-"$repo_root/build/github-ui-preview"}
mkdir -p "$output"
output=$(cd "$output" && pwd)
python3 "$repo_root/tests/ui_render/fixtures.py" "$output/fixtures"
cmake -S "$repo_root/tests/ui_render" -B "$output/build" -G Ninja -DBADGE_UI_CLIB_ALLOCATOR="${BADGE_UI_CLIB_ALLOCATOR:-OFF}"
cmake --build "$output/build" --parallel 4
for fixture in avatar.png avatar64.png avatar64.jpg avatar-progressive.jpg avatar-corrupt.png; do
    run_dir="$output/${fixture%.*}"
    if [[ $fixture == avatar64.jpg ]]; then run_dir="$output/avatar64-jpeg"; fi
    mkdir -p "$run_dir"
    if [[ $fixture == avatar-progressive.jpg || $fixture == avatar-corrupt.png ]]; then
        (cd "$run_dir" && "$output/build/badge_ui_render" "$output/fixtures/$fixture" reject)
    else
        (cd "$run_dir" && "$output/build/badge_ui_render" "$output/fixtures/$fixture")
    fi
done
printf 'Native LVGL previews and avatar/font checks: %s\n' "$output"
