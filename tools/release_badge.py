#!/usr/bin/env python3
"""Plan a manual semantic release and package only verified public firmware assets."""
from __future__ import annotations

import argparse
import hashlib
import html
import json
import re
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True
import archive_firmware as archive

VERSION = re.compile(r"v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)")
CONVENTIONAL = re.compile(r"^([a-z]+)(?:\([^\r\n()]+\))?(!)?:\s+(.+)$")
SHA = re.compile(r"[0-9a-f]{40}")


def version(value: str) -> tuple[int, int, int]:
    match = VERSION.fullmatch(value)
    if not match or len(value) > 20:
        raise ValueError("version must be X.Y.Z (optional v), without leading zeros or suffixes")
    return tuple(int(x) for x in match.groups())


def display(value: tuple[int, int, int]) -> str:
    return ".".join(map(str, value))


def git(root: Path, *args: str) -> str:
    return subprocess.run(["git", *args], cwd=root, check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout.strip()


def plan(root: Path, requested: str = "", target: str = "HEAD") -> dict:
    sha = git(root, "rev-parse", "--verify", target + "^{commit}")
    if not SHA.fullmatch(sha) or sha != git(root, "rev-parse", "HEAD"):
        raise ValueError("release target must equal the checked-out commit")
    if git(root, "rev-parse", "--is-shallow-repository") == "true":
        raise ValueError("release planning requires full history and all tags")
    tags: dict[tuple[int, int, int], str] = {}
    reachable = set(git(root, "tag", "--merged", sha).splitlines())
    for tag in git(root, "tag", "--list").splitlines():
        if not VERSION.fullmatch(tag):
            continue
        number = version(tag)
        if number in tags:
            raise ValueError("ambiguous duplicate semantic version tags")
        tags[number] = tag
    latest = max(tags, default=None)
    if latest is not None and tags[latest] not in reachable:
        raise ValueError("highest release tag is not reachable from this commit; resolve history first")
    previous = tags[latest] if latest is not None else None
    log_range = previous + ".." + sha if previous else sha
    raw = git(root, "log", "--format=%H%x00%B%x00", log_range)
    fields = raw.split("\0")
    commits = []
    bump = 0
    for index in range(0, len(fields) - 1, 2):
        commit, message = fields[index].strip(), fields[index + 1].strip()
        if not commit:
            continue
        subject = message.splitlines()[0] if message else ""
        match = CONVENTIONAL.match(subject)
        breaking = bool(re.search(r"(?m)^BREAKING[ -]CHANGE:\s*\S", message))
        kind = match.group(1) if match else "other"
        level = 3 if breaking or (match and match.group(2)) else 2 if kind == "feat" else 1 if kind in ("fix", "perf") else 0
        bump = max(bump, level)
        commits.append({"sha": commit, "subject": subject[:300], "type": kind, "level": level})
    if requested:
        chosen = version(requested)
        if latest is not None and chosen <= latest:
            raise ValueError("explicit version must be greater than every existing release tag")
    elif latest is None:
        chosen = version((root / "VERSION").read_text().strip())
    elif not bump:
        return {"schema": 1, "release": False, "sha": sha, "previous": previous,
                "reason": "No feat, fix, perf or breaking change since the last release.", "commits": commits}
    else:
        major, minor, patch = latest
        chosen = (major + 1, 0, 0) if bump == 3 else (major, minor + 1, 0) if bump == 2 else (major, minor, patch + 1)
    selected = display(chosen)
    if len("github-badge-" + selected) > 31:
        raise ValueError("version exceeds ESP application descriptor capacity")
    return {"schema": 1, "release": True, "sha": sha, "previous": previous,
            "version": selected, "tag": "v" + selected, "explicit": bool(requested), "commits": commits}


def notes(selected: dict) -> str:
    lines = [f"# GitHub Pixel Passport {selected['tag']}", "",
             f"Source / 源码提交: `{selected['sha']}`", "", "## Changes / 更新", ""]
    changes = [entry for entry in selected["commits"] if entry["level"]]
    for entry in changes:
        # Commit messages are content, never shell arguments or executable Markdown HTML.
        subject = html.escape(entry["subject"], quote=False).replace("[", "\\[").replace("]", "\\]")
        lines.append(f"- {subject} (`{entry['sha'][:12]}`)")
    if not changes:
        lines.append("- Initial or explicitly requested firmware release. / 首次或手动指定版本发布。")
    lines += ["", "## Install / 安装", "",
              "- Existing compatible badge: flash `app.bin` at `0x10000`; preserves NVS and badge cache.",
              "- 已安装兼容铭牌固件：将 `app.bin` 写入 `0x10000`，保留配置和缓存。",
              "- Blank device: flash `full.bin` at `0x0`. Full images can reset Wi-Fi/settings; export a backup first.",
              "- 首次安装：将 `full.bin` 写入 `0x0`。完整镜像可能重置配网与设置；升级前先导出备份。",
              "- Target: ESP32-C3, 8 MiB Flash, FoloToy AI Passport. Never flash an app-only image at 0x0.",
              "- Long OK toggles settings; short UP/DOWN navigates focus; long UP/DOWN changes pages.",
              "- 长按 OK 进入/退出设置，短按上下移动焦点，长按上下切页。",
              "", "Build: PASS · Host tests: PASS · Device tests: NOT RUN (CI)",
              "Build locally / 本地构建: activate ESP-IDF 5.5.3, then run `BADGE_VERSION=" + selected["version"] + " ./tools/validate.sh`.",
              "The tag's GitHub source archives contain the exact published source. / 标签源码归档对应本次固件。", ""]
    return "\n".join(lines)


def package(root: Path, selected: dict, destination: Path) -> None:
    if not selected.get("release") or not SHA.fullmatch(selected.get("sha", "")):
        raise ValueError("a release plan is required")
    number = display(version(selected["version"]))
    if selected["tag"] != "v" + number or git(root, "rev-parse", "HEAD") != selected["sha"]:
        raise ValueError("plan identity does not match checked-out source")
    full = archive.read_artifact(root / "build", archive.FULL_BIN)
    digest = hashlib.sha256(full).hexdigest()
    provenance = json.loads(archive.read_artifact(root / "build", "firmware-provenance.json"))
    if provenance != {"source_commit": selected["sha"], "full_bin_sha256": digest, "source_clean": True}:
        raise ValueError("build provenance is stale or came from a dirty checkout; rebuild the release commit")
    if git(root, "status", "--porcelain"):
        raise ValueError("release packaging requires a clean source checkout")
    bundle = root / "build" / "firmware" / digest
    checked = archive.verify_archive(bundle)
    if checked["app_descriptor"]["version"] != "github-badge-" + number:
        raise ValueError("firmware version does not match release plan")
    if checked["app_descriptor"]["idf_version"] != "v5.5.3" or checked["image_offsets"][archive.APP + ".bin"] != 0x10000:
        raise ValueError("unexpected release toolchain or application offset")
    prefix = "github-pixel-passport-v" + number
    files = {prefix + "-app.bin": archive.read_artifact(bundle, archive.APP + ".bin"),
             prefix + "-full.bin": full}
    manifest = {"schema": 1, "version": number, "tag": selected["tag"], "source_commit": selected["sha"],
                "target": checked["target"], "flash_size_bytes": checked["flash_size_bytes"],
                "idf_version": checked["app_descriptor"]["idf_version"],
                "application_version": checked["app_descriptor"]["version"],
                "full_bin_sha256": digest,
                "files": {name: {"size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                                  "flash_offset": "0x10000" if name.endswith("-app.bin") else "0x0"}
                          for name, data in files.items()}}
    files[prefix + "-manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    files["FLASHING.txt"] = (f"GitHub Pixel Passport {selected['tag']} / ESP32-C3 / 8 MiB\n"
        f"Upgrade compatible firmware: {prefix}-app.bin at 0x10000.\n"
        f"First install: {prefix}-full.bin at 0x0.\n"
        "Do not erase the whole chip for an ordinary app update.\n"
        "Full images may reset stored configuration. Export a backup before upgrading.\n"
        "App updates preserve NVS and the badge_data cache partition with the same layout.\n"
        "Never write app.bin at 0x0. Check SHA256SUMS.txt before flashing.\n"
        "兼容版本升级用 app.bin / 0x10000；首次安装用 full.bin / 0x0。\n"
        "完整镜像可能重置配置；先导出备份，普通升级无需整片擦除。\n"
        "CI build/host tests passed; hardware behavior still needs a real-device check.\n").encode()
    files["release-notes.md"] = notes(selected).encode()
    for name in ("LICENSE", "LICENSE-FoloToy", "THIRD-PARTY-LICENSES.txt"):
        files[name] = archive.read_artifact(root, name)
    files["SHA256SUMS.txt"] = "".join(f"{hashlib.sha256(data).hexdigest()}  {name}\n" for name, data in sorted(files.items())).encode()
    destination = archive.safe_path(destination)
    destination.mkdir(parents=True, exist_ok=False)
    for name, data in files.items():
        (destination / name).write_bytes(data)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("plan")
    command.add_argument("--version", default="")
    command.add_argument("--sha", default="HEAD")
    command.add_argument("--output", type=Path, required=True)
    command.add_argument("--github-output", type=Path)
    command = commands.add_parser("package")
    command.add_argument("--plan", type=Path, required=True)
    command.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "plan":
            selected = plan(args.root, args.version, args.sha)
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(selected, indent=2) + "\n")
            if args.github_output:
                with args.github_output.open("a") as output:
                    for key in ("release", "version", "tag", "sha"):
                        value = str(selected.get(key, "")).lower() if key == "release" else selected.get(key, "")
                        output.write(f"{key}={value}\n")
            print(selected.get("tag", selected.get("reason")))
        else:
            package(args.root, json.loads(args.plan.read_text()), args.output)
        return 0
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"Release error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
