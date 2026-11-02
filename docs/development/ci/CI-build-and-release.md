<p align="right"><a href="CI-build-and-release.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Manual badge build and release

This application has one workflow: [release.yml](../../../.github/workflows/release.yml).
Open **Actions → Build and publish badge firmware → Run workflow**, select the
repository's current default branch, and optionally enter a version. This manual
run builds and publishes a GitHub Release. Pushes, pull requests, tags and
schedules do not run workflows; there is no automatic upstream synchronization.

## Version selection

Leave the version blank for automatic selection. With no semantic release tags,
`VERSION` supplies the first version, currently `0.2.5`. Thereafter the highest
reachable `vX.Y.Z` tag is the baseline. Commit subjects and breaking-change
footers since that tag determine the next version:

| Change | Increment |
| --- | --- |
| `feat:` or `feat(scope):` | Minor |
| `fix:` or `perf:` | Patch |
| `type!:` / `type(scope)!:` or `BREAKING CHANGE:` / `BREAKING-CHANGE:` footer | Major |
| Documentation, tests, refactors and maintenance without the above markers | No release |

The largest increment wins; major resets minor/patch, and minor resets patch.
Before `1.0.0`, a breaking change still increments the major version. Squash-merge
subjects therefore need the appropriate Conventional Commit type. Release notes
list the corresponding feature, fix, performance and breaking-change subjects.

An explicit `X.Y.Z` (optional leading `v`) overrides automatic selection and may
publish a maintenance-only change. Prereleases, leading zeros and suffixes are
rejected. The selected version must exceed every semantic release tag. The
planner rejects duplicate version aliases, shallow history and a highest release
tag that is not reachable from the selected commit. It never moves a tag.

Local builds use `VERSION`. CI sets `BADGE_VERSION` to the selected version;
CMake embeds `github-badge-X.Y.Z` into the firmware and packaging verifies that
exact identity before naming files. `VERSION` is the initial/local baseline,
not a file that the workflow automatically commits after each release.

## Validation and permissions

The read-only build job checks out full history at the dispatch commit, runs
`tools/validate.sh --static`, then the firmware gate in ESP-IDF 5.5.3 for ESP32-C3.
The firmware gate includes collector integration tests, a clean isolated build,
real native LVGL rendering/avatar checks with the built-in allocator, merged-image
verification and content-addressed archival. All Actions use pinned commit SHAs.

Only the separate publishing job receives `contents: write`. It verifies asset
checksums, creates `refs/tags/vX.Y.Z` atomically at the exact dispatch SHA, then
uses `gh release create --draft --verify-tag --target <sha>` with prepared notes.
It uploads verified assets before publishing the draft. No force-push or automatic merge is used. Concurrent release
runs are serialized. The workflow never flashes a device.

If upload or publication fails, use **Re-run failed jobs** on the same Actions
run within 30 days. Build artifacts are retained for that retry window.
The publishing job retrieves the original build attempt's immutable artifact,
accepts an existing lightweight tag only when its SHA exactly matches that build,
and resumes an existing draft. Only draft assets may be replaced. Published
releases and tags pointing elsewhere are never overwritten. A new workflow run
still rejects duplicate versions; it does not silently republish an old release.

## Published assets

- `github-pixel-passport-vX.Y.Z-app.bin`: upgrade compatible badge firmware at
  `0x10000`, preserving NVS and the existing badge cache layout.
- `github-pixel-passport-vX.Y.Z-full.bin`: first installation at `0x0`; a merged
  image can reset Wi-Fi/settings. Export a backup before an upgrade.
- `github-pixel-passport-vX.Y.Z-manifest.json`: version, source SHA, sizes, hashes
  and flash offsets; no local paths or debug binaries.
- `SHA256SUMS.txt`, `FLASHING.txt`, `release-notes.md`, `LICENSE`, `LICENSE-FoloToy` and
  `THIRD-PARTY-LICENSES.txt` (complete dependency and font license texts).

The application-only image must never be written at `0x0`. GitHub's source archives
for the release tag identify the exact source. ELF/MAP files, raw logs, local
configuration and cached account data are excluded from public artifacts.
Matching debug files remain in the build's local content-addressed archive.

## Reproduce locally

```bash
./tools/validate.sh --static
source <path-to-esp-idf-v5.5.3>/export.sh
BADGE_VERSION=0.2.5 ./tools/validate.sh --firmware
python3 tools/release_badge.py plan --version 0.2.5 --output build/release-plan.json
python3 tools/release_badge.py package --plan build/release-plan.json --output build/release-assets
```

Planning requires a complete local Git history and tags. Packaging requires a
new destination directory and the verified archive matching
`build/FoloToy-AI-Passport-full.bin`. It verifies the archive and application
version again. The gate records the source commit, full-image hash and whether
the source was clean before and after building. Packaging rejects missing, stale
or dirty build provenance and requires a clean checkout. It does not replace the
validation gate or publish anything.

CI reports Build and Host tests separately from Device tests, which remain
**NOT RUN**. Check startup, buttons, network behavior and flashing on hardware
before describing a release as device-validated.
