#!/usr/bin/env bash
# Check native UI code with CI's compiler family; never builds firmware.
# Requires resolved managed components, CMake and Ninja, like render_badge_ui.sh.
set -euo pipefail
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
compiler=$(command -v "${BADGE_GCC:-gcc}") || {
    echo 'GCC not found. Set BADGE_GCC to a native GCC executable.' >&2
    exit 1
}
macros=$("$compiler" -dM -E -x c /dev/null)
if [[ $macros != *'__GNUC__'* || $macros == *'__clang__'* ]]; then
    echo 'This check requires GCC, not a Clang alias. Set BADGE_GCC (for example, gcc-13 on macOS).' >&2
    exit 1
fi
"$compiler" --version
output=$(mktemp -d /tmp/badge-ui-gcc.XXXXXX)
trap 'rm -rf -- "$output"' EXIT
# A new build directory ensures CMake cannot reuse a cached Clang selection.
CC="$compiler" "$repo_root/tools/render_badge_ui.sh" "$output"
