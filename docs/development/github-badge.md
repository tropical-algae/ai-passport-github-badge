<p align="right"><a href="github-badge.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# GitHub Pixel Passport application guide

GitHub Pixel Passport is a seven-page, 240 × 320 GitHub badge for FoloToy AI Passport: ESP32-C3, 8 MB Flash, no PSRAM, ESP-IDF 5.5.3. It uses public GitHub REST data, a local Wi-Fi setup portal and persistent offline storage. Start with the [project README](../../README.md) for installation and everyday use.

## Application structure

| Module | Responsibility |
| --- | --- |
| `main/badge_app.c` | Startup, background-job ownership, configuration handoff, scheduling and UI publication |
| `main/badge_input.c`, `badge_idle.c`, `badge_ui_state.c` | Button gestures, wake/timeout behavior and pure navigation state |
| `main/badge_ui.c` | LVGL drawing, page geometry, cached-avatar decoding, QR dialogs and feedback |
| `main/badge_network.c`, `badge_portal.c`, `badge_portal.html` | Wi-Fi, local HTTP/DNS setup, scans and form validation |
| `main/badge_sync.c`, `badge_sync_parse.c`, `badge_sync_logic.c` | Bounded HTTP collection, JSON parsing, quota/cursor logic and event deduplication |
| `main/badge_model.c`, `badge_storage.c`, `badge_backup.c` | Validated data, snapshots, NVS preferences and transactional backup/restore |
| `components/bsp` | Display, buttons, battery and other board support |

The baseline test-menu and demo sources are retained for reference but excluded from the badge build. Application logic stays outside the BSP. Button callbacks only enqueue edges; they do not perform networking or storage. UI access and mutation of the retained display model share the BSP LVGL lock. HTTP, parsing and persistent writes run outside drawing callbacks.

## Pages and controls

| Page | Content and interaction |
| --- | --- |
| PROFILE | Username, display name, avatar, company, location, website, join date, public repository/follower/following counts. Short OK toggles the homepage QR. Biography and total Stars are intentionally omitted. |
| REPOSITORIES | Leading three repositories by Stars, including forks. UP/DOWN selects a repository and its Stars, forks and watchers. |
| LANGUAGES | Original repositories, all repositories, or one of the leading five repositories. Shows the leading five languages and OTHER. Long center labels use an ellipsis. |
| ACTIVITY | Sunday-based 24-week grid of observed events, plus recent PushEvent frequency. Unknown, partial, known-zero and future days remain distinct. |
| PR/ISSUES | Authored public PR/issue statistics. UP/DOWN focuses a tab; OK activates it. |
| STARS/GISTS | Sampled starred repositories and public gists. UP/DOWN focuses a tab; OK activates it. |
| TIMELINE | Six recent public events. |

Hold UP/DOWN for 600 ms to change pages; hold OK to open or close settings. Short UP/DOWN changes the current selection. QR dialogs consume UP/DOWN and close with short OK. The PROFILE avatar retains its normal visual appearance while serving as the homepage QR action.

The main settings list is BRIGHTNESS, AUTO SCREEN OFF, COLOR THEME, CONNECT WI-FI and REFETCH DATA. Brightness choices are 100/80/60/40/20/10%; timeout choices are 1/5/10/30/60 minutes or NEVER, defaulting to 10 minutes. The five themes are Phosphor Green, Industrial Yellow, Ice Cyan, Amethyst and Copper Rose. Confirming any appearance choice returns to its corresponding main settings row, even when the selected value is unchanged. These submenus have no BACK row; long OK exits settings.

Inactivity is measured from the last button activity. Screen timeout stops the backlight PWM at a low level, sends LCD display-off and Sleep In, and pauses display refresh without stopping networking or sync. Any key wakes the LCD, redraws the full frame and waits for the display transfer before restoring the saved brightness; the entire wake gesture is consumed. Failed display transitions retain a blocked input state and retry at a bounded rate instead of reporting success or operating an invisible page. The reversible screen path keeps SPI, GPIO ownership and the application alive; it does not call the terminal deep-sleep helper. Preferences persist once an account is configured. Battery and local-clock indicators use placeholders when their values are unknown. The bundled 10 px fallback covers the documented CJK and punctuation inventory; unsupported characters, including emoji, use visible missing-glyph boxes.

## Display power and idle work

The default configuration enables ESP-IDF dynamic frequency scaling: the CPU
can fall to the 40 MHz crystal frequency while idle and run at 160 MHz for work.
The backlight uses the crystal clock so PWM stays stable as APB frequency changes.
See the official [power-management guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-reference/system/power_management.html)
and [LEDC clock and stop APIs](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-reference/peripherals/ledc.html).
The isolated validation/release builds use these defaults; an existing local
sdkconfig must also enable `CONFIG_PM_ENABLE` and `CONFIG_PM_DFS_INIT_AUTO`.

After the first screen sleep, a continuous monotonic-clock callback replaces
LVGL's 5 ms periodic tick. Only display refresh is paused; other LVGL timers
retain real time. Disabling the whole timer handler is avoided because LVGL 9.5
then returns a 1 ms wait to the port, causing frequent polling. Layout requests
cannot resume a sleeping display, and rejected SPI submissions release LVGL's
flush wait so a wake can report the error and retry.

The application waits up to 250 ms between idle checks while asleep; a key
interrupts that wait immediately. Ordinary network idle waits are up to one
second, shortened for connection deadlines. Setup, scans and backup operations
retain 100 ms checks, and queued network events wake the worker immediately.
No input wait holds the backup maintenance lock.

Screen-off keeps Wi-Fi, button detection and scheduled synchronization active;
it does not enter CPU light or deep sleep. Wi-Fi uses IDF's default minimum
modem-sleep mode, and an open setup hotspot still costs power. The badge does
not start the I2S/audio driver. These facts do not establish board current or
battery life: compare the same Wi-Fi, hotspot and brightness conditions before
and after the update, including current measurements where available. Residual
LCD visibility and sleep/wake behavior require device acceptance.

## Wi-Fi setup and account changes

An unconfigured badge opens a temporary WPA2 hotspot with generated credentials, shown in a Wi-Fi QR. Join from a phone and open `http://192.168.4.1` if the captive helper does not open it. The portal scans nearby 2.4 GHz networks, deduplicates SSIDs and displays up to 16 by signal strength. Rescan and manual entry support hidden networks.

Save a Wi-Fi SSID/password, public GitHub username and integer sync interval of 1–168 hours (default 6). The browser supplies its UTC offset for the display clock; this is a fixed offset, not an automatic daylight-saving timezone database. No GitHub password or token is accepted. The page does not return saved Wi-Fi passwords. A blank password retains the saved one only for the same SSID; an explicit open-network option handles networks without passwords. Enterprise or captive-login Wi-Fi is not implemented.

Connection is tested before replacing saved credentials. On successful connection and persistent saving, the device immediately closes the setup QR. Its hotspot remains for 60 seconds to let the phone acknowledge the result. REFETCH DATA can close it early and queue a fetch. GitHub HTTPS starts after portal shutdown to separate their peak memory requirements.

Opening CONNECT WI-FI during a fetch waits for that worker to finish. The dialog displays FETCH IN PROGRESS / SETUP AFTER FETCH. Closing it cancels only the pending hotspot request. A username change invalidates the previous account's in-flight collection and loads the new account's independent cache, or an empty view until its first fetch.

The portal footer reads `powered by TROPICALALGAE's codex`; only the name links to [TROPICALALGAE](https://github.com/tropical-algae), opening with `noopener noreferrer` in a new tab.

## Background collection and feedback

A triggered sync belongs to the background worker. Leaving settings, switching pages, changing appearance or turning the screen off does not cancel it. Valid network time is required before HTTPS. A fresh cached account does not refetch merely because the device rebooted; an overdue offline refresh runs after reconnection, subject to quota deadlines.

The collector requests the sections needed by the seven pages and persists checkpoints. Independent sections continue when another endpoint fails, and completed cached sections remain visible until replacements are ready. Profile data is followed by the avatar download; image URLs are never fetched by rendering. A checkpoint advances only after its snapshot is durably saved. Retrying an incomplete collection resumes failed or unfinished work rather than discarding completed sections.

REFETCH DATA shows start and completion feedback, distinguishing full success, partial public data, transport/TLS/HTTP errors, clock wait, quota wait and storage failure. Two-line feedback includes the endpoint/reason when available; quota countdowns update with time. Manual refetch can retry ordinary network failures immediately. Actual server-imposed core, search and avatar deadlines remain persistent and cannot be bypassed by rebooting or starting a new collection.

Requests go directly to GitHub and its avatar CDN. Restricted networks, including some connections in mainland China, may fail to reach them reliably. The badge retains its offline cache and reports the failed section; it does not provide a proxy. Unknown history or unavailable sections are not silently replaced with invented zeroes.

## Public-data boundaries

| Data | Meaning and bounds |
| --- | --- |
| Repositories | Paginated without an application count cap. Only the leading five are retained for page selection/language scopes; the repository page displays three. |
| Watchers | Uses `subscribers_count`, not `watchers_count`, which is a Stars alias. |
| Languages | Aggregate byte counts for original/all repositories and the leading five individual repositories. Up to 128 named aggregates; additional names enter OTHER and mark the result partial. The chart displays five plus OTHER. |
| Activity | Public events and PushEvent frequency grouped by UTC date, not commits or the GitHub contribution calendar. The 24-week history accumulates locally with deduplication. |
| PR/issue statistics | Authored-by-user searches across public repositories. `incomplete_results` marks partial statistics. Closed PRs exclude merged PRs in the distribution; merge rate uses all PRs. |
| Starred/gists | First 100 entries per endpoint, with sample/count labels and partial flags when capped; two entries are retained for detail display. |
| Timeline | Six recent public events, sharing activity collection. |
| Avatars | HTTPS `avatars.githubusercontent.com` only, no redirects; at most 24 KiB and 64 × 64 pixels. Baseline JPEG and PNG up to 8 bits per channel are supported, including RGB/RGBA, palette, grayscale, transparency and Adam7 interlacing. 16-bit PNG, progressive JPEG, GIF and WebP are safely rejected. Unsupported or corrupt images fall back to the placeholder when no valid avatar exists. |

GitHub's event timeline exposes at most 300 events from the past 30 days. Missing older history and gaps from offline periods or result caps remain unknown or partial; a 24-week layout does not imply a 24-week server history. See [GitHub event limits](https://docs.github.com/en/rest/activity/events#about-github-events).

Unauthenticated REST requests share a 60-request/hour primary limit per originating public IP; search endpoints have separate, stricter limits. A large repository/language collection can span several quota windows. See [REST rate limits](https://docs.github.com/en/rest/using-the-rest-api/rate-limits-for-the-rest-api) and [watching versus starring](https://docs.github.com/en/rest/activity/starring#starring-versus-watching).

HTTP bodies and individual JSON objects are bounded. Truncated/malformed responses fail validation; oversized events are skipped with partial coverage. A successful partial snapshot is usable data with an explicit coverage limit, not proof that every public record was fetched.

## Avatar and memory ownership

The encoded avatar is stored in Flash for offline use. The app reads and decodes it after the network worker releases its memory and before a queued hotspot starts. The UI copies the decoded image into its own 54 × 54 RGB565 buffer (with a separate alpha plane when required), and the app releases the encoded bytes after the call. Full-image sampling covers the entire source, including streaming JPEG decoder regions. JPEG bytes use LVGL's public memory-filesystem path; temporary decoder/cache references are closed before the borrowed input is freed.

A failed decode preserves an existing valid avatar and can be retried later; changing accounts clears the previous avatar. Rendering draws the owned pixels without downloading or repeatedly decoding the image. The opaque pixel buffer costs 5,832 bytes; optional alpha adds 2,916 bytes. Decode scratch state is heap allocated to protect the small application stack.

LVGL shares the normal IDF heap instead of reserving an idle fixed pool. The BSP uses one 240 × 40 RGB565 DMA display buffer. Network, portal and avatar-decoding peaks are separated; no PSRAM is assumed. Successful native allocation tests cannot establish real-device heap/stack margins.

The shared avatar gate validates the complete PNG IHDR, permitted color/bit-depth combinations, compression/filter methods and interlace mode before downloading to cache, accepting a backup, or invoking the UI decoder. The pinned LVGL 9.5.0 LodePNG adapter's `decodeGeneric()` allocates a four-byte-per-pixel buffer before `postProcessScanlines()` writes source-format pixels; RGB16/RGBA16 need six/eight bytes per pixel. Rejecting all 16-bit PNGs at the shared gate prevents that unsafe path without a vendor patch. Pixel-comparison fixtures cover accepted PNG formats; sanitizer regressions verify that 16-bit RGB/RGBA never reach the decoder.

## Persistence, backup and flashing

| Partition | Offset | Size | Purpose |
| --- | --- | --- | --- |
| `nvs` | `0x9000` | `0x6000` | Wi-Fi, preferences and active cache namespace |
| `phy_init` | `0xf000` | `0x1000` | Radio calibration data |
| `factory` | `0x10000` | `0x6f0000` | Badge application |
| `badge_data` | `0x700000` | `0x100000` | SPIFFS snapshots, avatars, event journal and sync cursors |

Snapshots use two slots with schema, account identity, generation and CRC checks. Staged file replacements retain recoverable backups; interrupted writes are recovered at boot. The newest valid slot is preferred without treating an I/O error as proof that a newer record is absent. SPIFFS existence probes use supported VFS operations. An existing nonempty partition that cannot mount is preserved; only an entirely erased badge-data partition is automatically formatted. Full storage reports failure and keeps older valid records. There is no automatic old-account-cache eviction.

EXPORT BACKUP and RESTORE BACKUP are separate from the ordinary setup form. Export includes the **saved** configuration, latest current-account model, avatar, accumulated activity and event deduplication IDs. It excludes unsaved form edits, other accounts and unfinished sync cursors. The Wi-Fi password is plaintext in the backup, which must be stored privately.

Imports are limited to 128 KiB and validate schema, CRC, strings, arrays, numeric bounds and journal integrity. Files are staged in an unused namespace; one NVS record activates configuration and namespace together. Invalid uploads or failed writes keep the current configuration active. An uncertain NVS commit retains both file sets. On success, the portal acknowledges the import and the badge restarts after three seconds using the imported Wi-Fi, username and interval. If that Wi-Fi is unavailable, imported data remains viewable offline; reopen setup to change the network. Backup/restore temporarily freezes settings and sync to keep the export/import consistent.

Legacy configurations without a timeout receive the 10-minute default and retain their cache namespace. Newer backups contain the selected timeout; firmware that does not support that extended format cannot import them.

Use an app image at `0x10000` to update a compatible installation without replacing NVS or the cache partition. First installation uses a full image at `0x0`; it replaces the bootloader/partition table, and merge padding may reset settings. Export before upgrading, check the release's layout/manifest, and do not use a routine full-chip erase. See the [release guide](ci/CI-build-and-release.md).

## Validation

With ESP-IDF 5.5.3 activated, run:

```bash
./tools/validate.sh --static
./tools/validate.sh
./tools/render_badge_ui.sh build/github-ui-preview
BADGE_UI_CLIB_ALLOCATOR=ON ./tools/render_badge_ui.sh build/github-ui-preview-clib
```

The gate covers repository rules, pure input/navigation/timing models, app-worker interactions, portal JavaScript, storage/backup fault injection, real-cJSON collector fixtures, isolated firmware build and merged-image verification. Native C/LVGL tests check page rendering, font coverage, QR generation, PNG/JPEG ownership, fallback paths and memory-pressure recovery. They use synthetic fixtures; their images are software previews, not device photographs or proof of a phone QR scan.

For hardware acceptance, check cold boot, all button gestures, setup scans and failed/successful association, automatic QR closure, background fetch after leaving settings, full/partial/error feedback, offline avatar after restart, timeout/wake, persisted preferences, backup recovery and accumulated-history deduplication. Observe heap/stack margins during AP, TLS and image operations. Report Build, Host tests and Device tests separately; a build does not substitute for device testing.

## Design reference

The supplied [240 × 320 HTML reference](../../assets/reference/github_pixel_240x320_v20.html) is preserved unchanged and is not embedded in firmware. Its SHA-256 is `1978243fac495c2fdf22561c2f5f5fb30b5075ed5ce359b61e0c705521c796f9`. Font licenses, source identities and preview regeneration instructions are in the [asset notes](../../assets/README.md).
