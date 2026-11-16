"""Run the resolved button component's real state machine with badge gestures.

The hardware-free functions, private types/macros, and BSP callback registration
are extracted verbatim, avoiding a second debounce or event mapping algorithm.
Requires managed_components, so this belongs to the firmware gate after IDF
has resolved dependencies; the check itself only compiles a native executable.
"""

import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "managed_components/espressif__button"


def between(source, start, end):
    assert source.count(start) == source.count(end) == 1, "Button source layout changed; review extraction"
    return source[source.index(start):source.index(end)]


def main():
    source = (COMPONENT / "iot_button.c").read_text()
    manifest = (COMPONENT / "idf_component.yml").read_text()
    assert re.search(r"^version: 4\.2\.0$", manifest, re.M), "Review gestures when upgrading the button component"
    config = (ROOT / "sdkconfig.defaults").read_text()
    kconfig = (COMPONENT / "Kconfig").read_text()
    options = ["PERIOD_TIME_MS", "DEBOUNCE_TICKS", "SHORT_PRESS_TIME_MS", "LONG_PRESS_TIME_MS", "LONG_PRESS_HOLD_SERIAL_TIME_MS"]
    defines = []
    for option in options:
        name = "BUTTON_" + option
        configured = re.search(rf"^CONFIG_{name}=(\d+)$", config, re.M)
        default = re.search(rf"config {name}\n(?:(?!\n    config)[\s\S])*?\n        default (\d+)", kconfig)
        value = configured[1] if configured else default[1]
        defines.append(f"#define CONFIG_{name} {value}\n")
    pins = (ROOT / "components/bsp/include/bsp_pins.h").read_text()
    bsp = (ROOT / "components/bsp/src/bsp_button.c").read_text()
    for name in ["BSP_BTN_SHORT_PRESS_MS", "BSP_BTN_LONG_PRESS_MS"]:
        value = re.search(rf"^#define {name}\s+(\d+)\b", pins, re.M)[1]
        defines.append(f"#define {name} {value}\n")
    extracted = "\n".join([
        '#include <stdio.h>\n#include <stdlib.h>\n#include "iot_button.h"',
        between(source, "#define BTN_CHECK", "static const char *button_event_str"),
        between(source, "enum {\n    PRESS_DOWN_CHECK", "//button handle list head."),
        between(source, "#define TICKS_INTERVAL", "static void button_process_pending_wake_disable"),
        between(source, "uint32_t iot_button_get_pressed_time(", "uint32_t iot_button_get_ticks_time("),
        '#include "bsp_button.h"\nstatic bool s_ready;\nstatic bsp_btn_cb_t s_cb;\nstatic void *s_user;',
        between(bsp, "static void on_event(", "// 初始化中途失败时"),
        between(bsp, "static esp_err_t register_callbacks(", "static esp_err_t button_init("),
        (ROOT / "tests/test_button_sampling.c").read_text(),
    ])
    with tempfile.TemporaryDirectory(prefix="badge-button-sampling-") as directory:
        temporary = Path(directory)
        (temporary / "sdkconfig.h").write_text("".join(defines))
        (temporary / "esp_err.h").write_text("#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 0x102\n")
        (temporary / "esp_log.h").write_text("#define ESP_LOGE(...) ((void)0)\n")
        unit = temporary / "button_sampling.c"
        unit.write_text('#include "esp_log.h"\n' + extracted)
        executable = temporary / "button_sampling"
        subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-sign-compare",
            "-I" + str(temporary), "-I" + str(COMPONENT / "include"),
            "-I" + str(COMPONENT / "interface"), "-I" + str(ROOT / "main"),
            "-I" + str(ROOT / "components/bsp/include"),
            str(unit), str(ROOT / "main/badge_input.c"), str(ROOT / "main/badge_idle.c"),
            "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
