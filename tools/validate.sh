#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware]" >&2
}

run_static_checks() {
    local actionlint_bin
    local test_dir

    python3 tools/check_repo.py

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    test_dir="$(mktemp -d /tmp/ai-passport-host-tests.XXXXXX)"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_pixel_math.c main/ui_pixel_math.c \
        -o "${test_dir}/test_ui_pixel_math"
    "${test_dir}/test_ui_pixel_math"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_demo_navigation.c main/demo_navigation.c \
        -o "${test_dir}/test_demo_navigation"
    "${test_dir}/test_demo_navigation"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_key_guard.c -o "${test_dir}/test_key_guard"
    "${test_dir}/test_key_guard"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_display_rounding.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_display_rounding"
    "${test_dir}/test_bsp_display_rounding"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_es8311_sleep_check.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_es8311_sleep_check"
    "${test_dir}/test_bsp_es8311_sleep_check"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/battery_stubs -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_battery.c -o "${test_dir}/test_bsp_battery"
    "${test_dir}/test_bsp_battery"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_button.c -o "${test_dir}/test_bsp_button"
    "${test_dir}/test_bsp_button"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_lvgl_init.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_lvgl_init"
    "${test_dir}/test_bsp_lvgl_init"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/audio_stubs -Icomponents/bsp/include -Icomponents/bsp/src \
        tests/test_bsp_audio_recovery.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_audio_recovery"
    "${test_dir}/test_bsp_audio_recovery"
    for demo in audio low_power ble wifi; do
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
            -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain \
            "tests/test_demo_${demo}_runtime.c" -Wl,--gc-sections \
            -o "${test_dir}/test_demo_${demo}_runtime"
        "${test_dir}/test_demo_${demo}_runtime"
    done
    for unit in core idle ui_state portal sync_parse sync_logic; do
        case "${unit}" in
            core) sources="main/badge_model.c main/badge_input.c" ;;
            idle) sources="main/badge_idle.c main/badge_input.c" ;;
            ui_state) sources="main/badge_model.c main/badge_ui_state.c" ;;
            portal) sources="main/badge_model.c main/badge_portal.c" ;;
            sync_parse) sources="main/badge_sync_parse.c" ;;
            sync_logic) sources="main/badge_model.c main/badge_sync_logic.c" ;;
        esac
        # These source paths are repository-owned constants, with no whitespace.
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
            "tests/test_badge_${unit}.c" ${sources} -o "${test_dir}/test_badge_${unit}"
        "${test_dir}/test_badge_${unit}"
    done
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/app_stubs -Imain \
        tests/test_badge_app.c main/badge_model.c main/badge_input.c \
        main/badge_idle.c main/badge_ui_state.c -o "${test_dir}/test_badge_app"
    "${test_dir}/test_badge_app"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/storage_stubs -Itests/bsp_stubs -Imain \
        tests/test_badge_storage.c main/badge_model.c -o "${test_dir}/test_badge_storage"
    "${test_dir}/test_badge_storage"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/storage_stubs -Itests/bsp_stubs -Imain \
        tests/test_badge_backup.c main/badge_model.c main/badge_sync_parse.c -o "${test_dir}/test_badge_backup"
    "${test_dir}/test_badge_backup"
    node tests/test_badge_portal_ui.js
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_battery_profile.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_deep_sleep_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_key_wake_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_check_repo.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_verify_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_archive_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_release_badge.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_install_passport_skills.py
    rm -rf "${test_dir}"
    echo "Host tests: PASS"
}

run_firmware_checks() (
    local validation_build_dir build_source_sha build_source_clean

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; activate ESP-IDF 5.5.3 first." >&2
        return 1
    fi

    build_source_sha="$(git rev-parse HEAD)"
    build_source_clean=false
    if [[ -z "$(git status --porcelain)" ]]; then build_source_clean=true; fi
    validation_build_dir="$(mktemp -d /tmp/ai-passport-firmware.XXXXXX)"
    trap 'case "${validation_build_dir}" in /tmp/ai-passport-firmware.*) rm -rf -- "${validation_build_dir}" ;; esac' EXIT

    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Dtime=badge_test_time \
        -Itests/sync_stubs -Imain -I"${IDF_PATH}/components/json/cJSON" \
        tests/test_badge_sync_integration.c main/badge_sync.c main/badge_sync_parse.c \
        main/badge_sync_logic.c main/badge_model.c \
        "${IDF_PATH}/components/json/cJSON/cJSON.c" -lm \
        -o "${validation_build_dir}/test_badge_sync_integration"
    "${validation_build_dir}/test_badge_sync_integration"

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults" \
        idf.py -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
    ./tools/render_badge_ui.sh "${validation_build_dir}/native-ui"
    idf.py -B "${validation_build_dir}" merge-bin \
        -o "${validation_build_dir}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${validation_build_dir}"
    PYTHONDONTWRITEBYTECODE=1 python3 tools/archive_firmware.py create \
        "${validation_build_dir}" --archive-root "${repo_root}/build/firmware"
    mkdir -p "${repo_root}/build"
    install -m 0644 \
        "${validation_build_dir}/FoloToy-AI-Passport-full.bin" \
        "${repo_root}/build/FoloToy-AI-Passport-full.bin"
    python3 - "${build_source_sha}" "${build_source_clean}" <<'PYPROVENANCE'
import hashlib
import json
import subprocess
import sys
from pathlib import Path
sha, clean = sys.argv[1:]
current = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
unchanged = not subprocess.check_output(["git", "status", "--porcelain"], text=True).strip()
provenance = {"source_commit": sha,
              "full_bin_sha256": hashlib.sha256(Path("build/FoloToy-AI-Passport-full.bin").read_bytes()).hexdigest(),
              "source_clean": clean == "true" and unchanged and sha == current}
Path("build/firmware-provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
PYPROVENANCE
    echo "Firmware build: PASS"
)

cd "${repo_root}"
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac
