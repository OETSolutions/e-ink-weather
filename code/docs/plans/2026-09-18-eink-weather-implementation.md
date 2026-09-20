# E-Ink Weather Display — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build firmware (ESP-IDF on the Good Display ESP32-M1) that renders a
configurable weather + Home Assistant dashboard onto a 920×680 e-ink panel, plus a web
app that designs that dashboard visually and pushes it to the device.

**Architecture:** Two deliverables behind one config contract. The web app owns the
layout and pre-renders the *static* layer to a 1 bpp 78,200-byte bitmap; the firmware
stores that bitmap, blits it each refresh, and draws only *dynamic values* on top from
pluggable data sources (OpenWeatherMap, HA REST/MQTT). A renderer interface keeps a
full on-device renderer possible later without redesign.

**Tech Stack:** ESP-IDF (PlatformIO, `framework = espidf`, pinned) · `esp_http_server`,
`esp_http_client` + CA bundle, `mqtt`, `esp_https_ota`, `wifi_provisioning`, `nvs_flash`,
`esp_sleep`, `cJSON` · Unity + pytest for on-device tests; a `native` PlatformIO env for
host unit tests · Web app: vanilla TypeScript + Vite, npm.

**Spec:** `docs/specs/2026-09-18-eink-weather-display.md` — **the executor must read it
alongside this plan.** Every step below cites the spec IDs (`HW-x`, `FR-x`, `NFR-x`,
`IF-x`) it satisfies.

## Global Constraints

Copied verbatim from the spec — every task implicitly includes these:

- **Panel/framebuffer:** 920×680 px, 1 bpp, **78,200 bytes**, MSB-first, row-major
  (HW-6). Panel is purely reflective; landscape; 198 dpi.
- **Panel lifetime:** refresh at least **every 24 h** or ghosting occurs (FR-10);
  always issue deep-sleep after an update and re-init before every full update (FR-12).
- **Undocumented vendor registers:** the EPD init sequence (`0x00 PSR=0x27,0x0E`,
  `0x30`, `0x62 HTOTAL`, `0x65 GSST`, `0xE0`, `0xE6`, `0xE7`, `0xE9`) is
  **vendor-proprietary and MUST be reused verbatim** (HW-5). Never rewrite it from the
  SSD2677 datasheet.
- **Pin map (verified):** BUSY=13, RES=12, D/C=14, CS=27, SCLK=18, SDI=23, SD CS=21,
  VBAT sense=26, KEY=25.
- **ADC2 caveat:** GPIO26 is ADC2_CH9 → **read battery voltage with WiFi OFF** (HW-3).
- **Framework:** `framework = espidf`, board `esp32dev`, version pinned (FR-35).
- **Target:** ESP32-WROOM-32D, 4 MB flash, ~320 KB SRAM, **no PSRAM** (NFR-2).
- **Secrets never in git.** Captive portal + BLE provisioning → NVS (FR-30).
- **Tests before code.** No requirement is "done" without a failing-then-passing test
  (NFR-6, NFR-7, NFR-8).
- **Build layout (VERIFIED by building both paths — do not deviate):** shared pure logic
  goes in `firmware/lib/<name>/` with a `library.json`; that is the only place **both**
  the `native` test runner and the ESP-IDF build compile. Tests go in
  `firmware/test/test_<suite>/`, **one `main()` per suite directory**. The first
  `pio run -e esp32dev` requires the one-time machine setup in Task 1 Step 3b.
- **Task stacks are sized explicitly** for TLS work; never rely on a default (NFR-1).
- **Repo root for new code:** `eink_weather/code/` — i.e. `firmware/`, `webapp/`, `docs/`.

---

## File Structure

Decomposition decisions locked here. Each file has one responsibility.

```
code/
  firmware/
    platformio.ini                 # envs: esp32dev (IDF 5.5.3) + native (host unit tests)
    partitions.csv                 # dual-OTA + NVS + bitmap storage
    sdkconfig.defaults             # pinned IDF config
    CMakeLists.txt
    src/main.c                     # boot sequence FR-28 (ESP-IDF entry point)
    lib/                           # PURE LOGIC — compiled by BOTH the native and IDF builders
      layout/                      #   (VERIFIED: lib/<n>/ with library.json works in both)
        library.json
        include/layout.h           # config model, widget geometry, page rotation
        src/layout.c
        src/render.c               # composite: blit static + draw dynamic fields
        src/fonts.c                # 1-bit atlas blitting
      alerts/                      # threshold + OWM alert evaluation
        library.json  include/alerts.h  src/alerts.c
      power/                       # VBAT sense maths, mode detection
        library.json  include/power.h   src/power.c
      devcfg/                      # config schema + migration
        library.json  include/devcfg.h  src/devcfg.c
      datasrc/                     # data-source interface + pure parsing
        library.json  include/datasrc.h
        src/src_owm.c  src/src_ha_rest.c  src/src_ha_mqtt.c
    test/                          # ONE main() per test_<suite>/ dir (VERIFIED rule)
      test_layout/         test_layout.c
      test_render_golden/  test_render_golden.c   # golden-image test (NFR-9)
      test_alerts/         test_alerts.c
      test_power_math/     test_power_math.c
      test_devcfg_migrate/ test_devcfg_migrate.c
      test_owm_parse/      test_owm_parse.c
      test_ha_parse/       test_ha_parse.c
    components/                    # HARDWARE-ONLY IDF components (not host-tested)
      epd/                         # EPD driver: vendor init + 1→2bpp + BUSY waits
        CMakeLists.txt  include/epd.h  epd.c  epd_spi.c
      net/                         # wifi station + reconnect + TLS + CA bundle
      api/                         # esp_http_server: config API, bitmap upload, OTA
      prov/                        # captive portal + BLE wifi_provisioning
    tools/
      gen_font_atlas.py            # build-time 1-bit atlas generator (FR-4a)
      owm_fixtures/                # recorded OWM JSON for tests
      ha_fixtures/                 # recorded HA responses
  webapp/
    package.json  vite.config.ts  tsconfig.json
    src/
      model/config.ts              # IF-1 typed config + schemaVersion + migration
      canvas/editor.ts             # drag/resize/snap interaction
      canvas/render.ts             # 1-bit renderer (mirrors firmware via shared fixtures)
      data/owm.ts  data/ha.ts      # data binding + HA entity validation
      alerts/rules.ts              # alert rule editor (mirrors firmware evaluation)
      transfer/bitmap.ts           # chunked bitmap encode + upload (IF-2a)
      transfer/config.ts           # JSON save/load (FR-26a)
      ui/…                         # panels, map picker, pages
    test/                          # Vitest unit + Playwright e2e
  docs/specs/  docs/plans/         # this spec + this plan + schema/api docs
```

**Why these boundaries [VERIFIED by building both paths]:**
- **Pure logic lives in `lib/<name>/`**, which is the **only** layout that the native test
  runner *and* the ESP-IDF builder both compile. Putting it in `components/` breaks
  `pio test -e native`; putting it only in `src/` hides it from tests. This was tested,
  not assumed.
- **Tests live in `test/test_<suite>/`, one `main()` per directory.** PlatformIO treats
  each subdirectory of `test/` as one test program; two `main()`s in one directory is a
  link error. Also verified, not assumed.
- `lib/layout`, `lib/alerts`, `lib/power`, `lib/devcfg` and the `lib/datasrc` parsers
  carry **no IDF includes**, so they host-test the bulk of the requirements (NFR-6).
  Data-source *parsing* is pure and lives there; the *transport* (TLS/HTTP/MQTT) does not.
- Hardware is quarantined in `components/` (IDF-only): `epd`, `net`, `api`, `prov`.
  These are exercised by on-device tests and manual HIL checks (NFR-8 group 2).

---

## Phase 0 — Scaffolding (no hardware needed)

### Task 1: Firmware project skeleton with a host test env

Establishes that `pio run` and `pio test -e native` both work **before** any real code
exists. Nothing else in the plan is verifiable until this passes.

**Files:**
- Create: `firmware/platformio.ini`
- Create: `firmware/CMakeLists.txt`
- Create: `firmware/partitions.csv`
- Create: `firmware/sdkconfig.defaults`
- Create: `firmware/lib/layout/library.json`
- Create: `firmware/lib/layout/include/layout.h`
- Create: `firmware/lib/layout/src/layout.c`
- Create: `firmware/test/test_layout/test_layout.c`
- Create: `firmware/src/main.c`
- Create: `firmware/.gitignore`

**Interfaces:**
- Produces: `layout_version(void) -> const char *` — a trivial function whose only job is
  to prove both build paths compile the same `lib/` source.

- [ ] **Step 1: Write the failing test**

`firmware/test/test_layout/test_layout.c` — note it lives in a **`test_<suite>/`
subdirectory** with exactly one `main()` (verified layout: PlatformIO treats each
subdirectory of `test/` as a separate test program):
```c
#include <string.h>
#include "unity.h"
#include "layout.h"

void setUp(void) {}
void tearDown(void) {}

static void test_layout_version_is_nonempty(void)
{
    const char *v = layout_version();
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_TRUE(strlen(v) > 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_layout_version_is_nonempty);
    return UNITY_END();
}
```

- [ ] **Step 2: Write the minimal implementation**

`firmware/lib/layout/include/layout.h`:
```c
#pragma once

/* Returns the layout component's semantic version string. */
const char *layout_version(void);
```

`firmware/lib/layout/src/layout.c`:
```c
#include "layout.h"

const char *layout_version(void)
{
    return "0.1.0";
}
```

`firmware/lib/layout/library.json` (makes PlatformIO build this library for **both**
the `native` and `esp32dev` environments — verified):
```json
{
  "name": "layout",
  "version": "0.1.0",
  "build": { "srcDir": "src", "includeDir": "include" }
}
```

- [ ] **Step 3: Configure both environments**

> **VERIFIED-WORKING configuration.** This exact combination was built end-to-end to a
> successful `firmware.bin` on this machine. Do not "improve" it.

`firmware/platformio.ini`:
```ini
[platformio]
default_envs = esp32dev

; ---- Host unit tests: pure logic, no hardware (NFR-6) ----
[env:native]
platform = native
test_framework = unity
test_filter = test_*
; Third-party host-testable deps go HERE, not in a library.json `dependencies`
; block — the latter fails to resolve for the native env (verified).
lib_deps =
    throwtheswitch/Unity@^2.6.1
    baracodadailyhealthtech/cJSON@^1.7.18

; ---- Device firmware (FR-35). IDF pinned deliberately — see Global Constraints ----
[env:esp32dev]
platform = platformio/espressif32
board = esp32dev
framework = espidf
; ESP-IDF 5.5.3. Do NOT bump to 6.x: IDF 6.x requires toolchain
; esp-15.2.0_20251204 but PlatformIO ships GCC 14.2.0, which fails the version check.
platform_packages = platformio/framework-espidf @ ~3.50503.0
board_build.partitions = partitions.csv
monitor_speed = 115200
build_flags = -Wall -Wextra
test_framework = unity
```
> On the device, JSON comes from ESP-IDF's bundled `cJSON` component, so the `lib_deps`
> entry above exists purely to make the same parsing code host-testable. Keep the parser
> in `lib/` free of `esp_*` includes so both paths compile it.

`firmware/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(eink_weather)
```

`firmware/partitions.csv` (dual-OTA + NVS + dedicated bitmap store, IF-2a).
**Layout verified: exactly fills 4 MB with no overlap; two 1.5 MiB app slots allow
OTA + rollback (FR-32) while leaving 896 KiB for the static bitmap and configs.**
```csv
# Name,   Type, SubType, Offset,   Size,     Flags
nvs,      data, nvs,     0x9000,   0x6000,
otadata,  data, ota,     0xf000,   0x2000,
phy_init, data, phy,     0x11000,  0x1000,
ota_0,    app,  ota_0,   0x20000,  0x180000,
ota_1,    app,  ota_1,   0x1A0000, 0x180000,
storage,  data, spiffs,  0x320000, 0xE0000,
```

`firmware/sdkconfig.defaults` (both lines are required — omitting the flash size triggers
a *"Flash memory size mismatch"* warning from IDF):
```ini
# Target ESP32-WROOM-32D: 4MB flash, no PSRAM (NFR-2)
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
# OTA rollback protection (FR-32)
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
```

`firmware/src/main.c` (placeholder that compiles and links; real boot sequence is Task 12).
Note: with PlatformIO's ESP-IDF builder the entry point is **`src/main.c`** and it **must**
define `app_main`, or the link fails with `undefined reference to 'app_main'`.
```c
#include "layout.h"

void app_main(void)
{
    (void)layout_version();
}
```

`firmware/.gitignore`:
```
.pio/
sdkconfig.esp32dev
```

- [ ] **Step 3b: One-time machine prerequisites (NOT optional — the build fails without these)**

Four separate failures were hit and fixed while verifying this task. Run these **once**
per machine before the first `pio run`:

```bash
# 1) PlatformIO creates ~/.platformio/penv/.espidf-<ver>/ but leaves it EMPTY.
#    Without this: "ModuleNotFoundError: No module named 'idf_component_manager'"
#    then "No module named kconfgen".
V=5.5.3
~/.platformio/penv/.espidf-$V/bin/python -m pip install idf-component-manager \
  -r ~/.platformio/packages/framework-espidf@3.50503.0/tools/requirements/requirements.core.txt

# 2) pio itself runs from Homebrew's Python here, not from ~/.platformio/penv.
#    Without this: esptool fails with "No module named 'intelhex'" while creating
#    bootloader.bin. Check the real interpreter first:
head -1 "$(which pio)"      # -> #!/opt/homebrew/Cellar/platformio/6.2.0/libexec/bin/python
/opt/homebrew/Cellar/platformio/6.2.0/libexec/bin/python -m pip install intelhex
```

- [ ] **Step 4: Run BOTH build paths and verify they pass**

Host tests:
```bash
cd firmware && pio test -e native
```
Expected: `1 Tests 0 Failures 0 Ignored  OK`

Firmware build (this is the step that catches IDF/toolchain problems early):
```bash
cd firmware && pio run -e esp32dev
```
Expected: `========================= [SUCCESS] =========================`
and `.pio/build/esp32dev/firmware.bin` exists.

- [ ] **Step 5: Commit**

```bash
git add firmware/
git commit -m "build: firmware skeleton with host (native) unit-test env and pinned ESP-IDF 5.5.3"
```

---

## Phase 1 — Host-testable core (the bulk of the tests, no hardware)

### Task 2: Config model + schema migration

**Files:**
- Create: `firmware/lib/devcfg/library.json`
- Create: `firmware/lib/devcfg/include/devcfg.h`
- Create: `firmware/lib/devcfg/src/devcfg.c`
- Create: `firmware/test/test_devcfg_migrate/test_devcfg_migrate.c`

**Interfaces:**
- Consumes: nothing.
- Produces: `#define DEVCFG_SCHEMA_VERSION 1`; `devcfg_migrate(int from, int to, const char *in, char **out) -> int` (0 = ok), which upgrades a config JSON document. Later tasks (Task 10 API, Task 16 webapp model) rely on the version constant and on migration being idempotent.

- [ ] **Step 1: Write the failing tests**

`firmware/test/test_devcfg_migrate/test_devcfg_migrate.c` (one `main()` for this suite —
verified requirement):
```c
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "devcfg.h"

void setUp(void) {}
void tearDown(void) {}

static void test_same_version_is_passthrough(void)
{
    const char *in = "{\"schemaVersion\":1}";
    char *out = NULL;
    TEST_ASSERT_EQUAL_INT(0, devcfg_migrate(1, 1, in, &out));
    TEST_ASSERT_EQUAL_STRING(in, out);
    free(out);
}

static void test_unknown_newer_version_is_refused(void)
{
    char *out = NULL;
    /* A config from the future must be refused, not silently mis-read (FR-26b). */
    TEST_ASSERT_NOT_EQUAL(0, devcfg_migrate(99, DEVCFG_SCHEMA_VERSION,
                                           "{\"schemaVersion\":99}", &out));
    TEST_ASSERT_NULL(out);
}

static void test_migrate_is_idempotent(void)
{
    char *once = NULL, *twice = NULL;
    TEST_ASSERT_EQUAL_INT(0, devcfg_migrate(1, DEVCFG_SCHEMA_VERSION,
                                           "{\"schemaVersion\":1}", &once));
    TEST_ASSERT_EQUAL_INT(0, devcfg_migrate(1, DEVCFG_SCHEMA_VERSION, once, &twice));
    TEST_ASSERT_EQUAL_STRING(once, twice);
    free(once); free(twice);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_same_version_is_passthrough);
    RUN_TEST(test_unknown_newer_version_is_refused);
    RUN_TEST(test_migrate_is_idempotent);
    return UNITY_END();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd firmware && pio test -e native -f test_devcfg*`
Expected: FAIL — `devcfg.h` not found / `devcfg_migrate` undefined.

- [ ] **Step 3: Write the minimal implementation**

`firmware/lib/devcfg/include/devcfg.h`:
```c
#pragma once

#define DEVCFG_SCHEMA_VERSION 1

/* Upgrade a config JSON document from `from` to `to`.
 * Returns 0 on success and sets *out to a malloc'd string the caller frees.
 * Returns non-zero and leaves *out NULL on any unsupported/unknown version. */
int devcfg_migrate(int from, int to, const char *in_json, char **out_json);
```

`firmware/lib/devcfg/src/devcfg.c`:
```c
#include "devcfg.h"
#include <stdlib.h>
#include <string.h>

static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) {
        memcpy(p, s, n);
    }
    return p;
}

int devcfg_migrate(int from, int to, const char *in_json, char **out_json)
{
    *out_json = NULL;
    if (!in_json) {
        return -1;
    }
    if (from > to || from < 1) {
        return -2;      /* from the future, or nonsense: refuse */
    }
    if (to != DEVCFG_SCHEMA_VERSION) {
        return -3;      /* we only know how to reach our own version */
    }
    /* v1 is the first version, so there is nothing to rewrite yet. */
    *out_json = dup_str(in_json);
    return *out_json ? 0 : -4;
}
```

`firmware/lib/devcfg/library.json`:
```json
{
  "name": "devcfg",
  "version": "0.1.0",
  "build": { "srcDir": "src", "includeDir": "include" }
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd firmware && pio test -e native`
Expected: all devcfg tests PASS.

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/devcfg
git commit -m "feat(devcfg): versioned config migration with safe refusal of unknown versions"
```

---

### Task 3: 1 bpp framebuffer + geometry primitives (HW-6)

This is the contract every later rendering task depends on, so it is pinned first.

**Files:**
- Create: `firmware/lib/layout/library.json` (already created in Task 1; extend if needed)
- Create: `firmware/lib/layout/include/canvas.h`
- Create: `firmware/lib/layout/src/canvas.c`
- Create: `firmware/test/test_canvas/test_canvas.c`

**Interfaces:**
- Produces:
  - `#define EPD_WIDTH 920`, `#define EPD_HEIGHT 680`, `#define EPD_FB_BYTES 78200`
  - `typedef struct { uint8_t *px; size_t len; } canvas_t;`
  - `canvas_init(canvas_t *c, uint8_t *buf)` — 1 bpp, MSB-first, row-major
  - `canvas_set_px(canvas_t*, int x, int y, int black)` — writes bit; white = 1
  - `canvas_get_px(const canvas_t*, int x, int y) -> int`
  - `canvas_fill(canvas_t*, int black)`
  - `canvas_blit_1bpp(canvas_t*, int x, int y, const uint8_t *src, int src_w, int src_h)`
  - `canvas_blit_1bpp_masked(canvas_t*, int x, int y, const uint8_t *src, int src_w, int src_h, int transparent_white)`

- [ ] **Step 1: Write the failing tests**

`firmware/test/test_canvas/test_canvas.c`:
```c
#include <string.h>
#include <stdlib.h>
#include "unity.h"
#include "canvas.h"

static canvas_t c;
static uint8_t *buf;

void setUp(void)
{
    buf = malloc(EPD_FB_BYTES);
    canvas_init(&c, buf);
}

void tearDown(void) { free(buf); }

static void test_buffer_size_is_exact(void)
{
    TEST_ASSERT_EQUAL_INT(920, EPD_WIDTH);
    TEST_ASSERT_EQUAL_INT(680, EPD_HEIGHT);
    /* 920 * 680 / 8 == 78200 exactly (HW-6) */
    TEST_ASSERT_EQUAL_INT(78200, EPD_FB_BYTES);
    TEST_ASSERT_EQUAL_INT(78200, EPD_WIDTH * EPD_HEIGHT / 8);
}

static void test_fill_white_sets_all_bits(void)
{
    canvas_fill(&c, 0);           /* 0 == white */
    for (size_t i = 0; i < EPD_FB_BYTES; i++) {
        TEST_ASSERT_EQUAL_UINT8(0xFF, buf[i]);
    }
}

static void test_set_px_roundtrip_msb_first(void)
{
    canvas_fill(&c, 0);
    canvas_set_px(&c, 0, 0, 1);   /* black, MSB of byte 0 */
    TEST_ASSERT_EQUAL_UINT8(0x7F, buf[0]);
    TEST_ASSERT_EQUAL_INT(1, canvas_get_px(&c, 0, 0));
    TEST_ASSERT_EQUAL_INT(0, canvas_get_px(&c, 1, 0));

    /* x=7 is the LSB of byte 0; x=8 starts byte 1 */
    canvas_set_px(&c, 7, 0, 1);
    TEST_ASSERT_EQUAL_UINT8(0x7E, buf[0]);
    canvas_set_px(&c, 8, 0, 1);
    TEST_ASSERT_EQUAL_UINT8(0x7F, buf[1]);
}

static void test_row_pitch_is_width_over_8(void)
{
    canvas_fill(&c, 0);
    canvas_set_px(&c, 0, 1, 1);   /* row 1 => byte 920/8 == 115 */
    TEST_ASSERT_EQUAL_UINT8(0x7F, buf[115]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, buf[114]);
}

static void test_blit_respects_destination_offset(void)
{
    canvas_fill(&c, 0);
    uint8_t src[2] = {0x00, 0x00};   /* 16 black px, 2 bytes wide */
    canvas_blit_1bpp(&c, 3, 5, src, 16, 1);
    TEST_ASSERT_EQUAL_INT(1, canvas_get_px(&c, 3, 5));
    TEST_ASSERT_EQUAL_INT(1, canvas_get_px(&c, 18, 5));
    TEST_ASSERT_EQUAL_INT(0, canvas_get_px(&c, 2, 5));
    TEST_ASSERT_EQUAL_INT(0, canvas_get_px(&c, 19, 5));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_buffer_size_is_exact);
    RUN_TEST(test_fill_white_sets_all_bits);
    RUN_TEST(test_set_px_roundtrip_msb_first);
    RUN_TEST(test_row_pitch_is_width_over_8);
    RUN_TEST(test_blit_respects_destination_offset);
    return UNITY_END();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd firmware && pio test -e native -f test_canvas`
Expected: FAIL — `canvas.h` not found.

- [ ] **Step 3: Write the minimal implementation**

`firmware/lib/layout/include/canvas.h`:
```c
#pragma once

#include <stddef.h>
#include <stdint.h>

#define EPD_WIDTH    920
#define EPD_HEIGHT   680
#define EPD_PITCH    (EPD_WIDTH / 8)          /* 115 bytes per row */
#define EPD_FB_BYTES ((size_t)EPD_PITCH * EPD_HEIGHT)   /* 78,200 (HW-6) */

typedef struct {
    uint8_t *px;
    size_t   len;
} canvas_t;

void canvas_init(canvas_t *c, uint8_t *buf);
void canvas_fill(canvas_t *c, int black);
void canvas_set_px(canvas_t *c, int x, int y, int black);
int  canvas_get_px(const canvas_t *c, int x, int y);
void canvas_blit_1bpp(canvas_t *c, int x, int y,
                      const uint8_t *src, int src_w, int src_h);
void canvas_blit_1bpp_masked(canvas_t *c, int x, int y,
                             const uint8_t *src, int src_w, int src_h,
                             int transparent_white);
```

`firmware/lib/layout/src/canvas.c`:
```c
#include "canvas.h"
#include <string.h>

/* Bit convention (HW-6): 1 = white, 0 = black; MSB-first within each byte;
 * row-major. This matches the vendor demo's 1 bpp 78,200-byte arrays. */

void canvas_init(canvas_t *c, uint8_t *buf)
{
    c->px = buf;
    c->len = EPD_FB_BYTES;
}

void canvas_fill(canvas_t *c, int black)
{
    memset(c->px, black ? 0x00 : 0xFF, c->len);
}

void canvas_set_px(canvas_t *c, int x, int y, int black)
{
    if (x < 0 || y < 0 || x >= EPD_WIDTH || y >= EPD_HEIGHT) {
        return;
    }
    size_t idx = (size_t)y * EPD_PITCH + (size_t)(x >> 3);
    uint8_t mask = (uint8_t)(0x80u >> (x & 7));
    if (black) {
        c->px[idx] &= (uint8_t)~mask;
    } else {
        c->px[idx] |= mask;
    }
}

int canvas_get_px(const canvas_t *c, int x, int y)
{
    if (x < 0 || y < 0 || x >= EPD_WIDTH || y >= EPD_HEIGHT) {
        return 0;
    }
    size_t idx = (size_t)y * EPD_PITCH + (size_t)(x >> 3);
    uint8_t mask = (uint8_t)(0x80u >> (x & 7));
    return (c->px[idx] & mask) ? 0 : 1;   /* bit clear => black => 1 */
}

static void blit(canvas_t *c, int x, int y,
                 const uint8_t *src, int src_w, int src_h, int transp)
{
    int pitch = (src_w + 7) / 8;
    for (int sy = 0; sy < src_h; sy++) {
        for (int sx = 0; sx < src_w; sx++) {
            uint8_t b = src[(size_t)sy * pitch + (size_t)(sx >> 3)];
            int black = (b & (0x80u >> (sx & 7))) ? 0 : 1;
            if (transp && !black) {
                continue;
            }
            canvas_set_px(c, x + sx, y + sy, black);
        }
    }
}

void canvas_blit_1bpp(canvas_t *c, int x, int y,
                      const uint8_t *src, int src_w, int src_h)
{
    blit(c, x, y, src, src_w, src_h, 0);
}

void canvas_blit_1bpp_masked(canvas_t *c, int x, int y,
                             const uint8_t *src, int src_w, int src_h,
                             int transparent_white)
{
    blit(c, x, y, src, src_w, src_h, transparent_white);
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd firmware && pio test -e native -f test_canvas`
Expected: all 5 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/layout firmware/test/test_canvas
git commit -m "feat(layout): 1bpp 920x680 canvas with MSB-first pixel and blit primitives"
```

---

### Task 4: Alert rule evaluation (FR-14 mechanism 1)

**Files:**
- Create: `firmware/lib/alerts/library.json`
- Create: `firmware/lib/alerts/include/alerts.h`
- Create: `firmware/lib/alerts/src/alerts.c`
- Create: `firmware/test/test_alerts/test_alerts.c`

**Interfaces:**
- Produces:
  - `typedef enum { ALERT_NONE, ALERT_ADVISORY, ALERT_WARNING, ALERT_SEVERE } alert_level_t;`
  - `typedef enum { ALERT_OP_GT, ALERT_OP_GTE, ALERT_OP_LT, ALERT_OP_LTE, ALERT_OP_EQ, ALERT_OP_NE } alert_op_t;`
  - `typedef struct { alert_op_t op; double threshold; alert_level_t level; } alert_rule_t;`
  - `alert_level_t alerts_eval(const alert_rule_t*, double value)`
  - `alert_level_t alerts_eval_all(const alert_rule_t*, int n, double value)`
  - `const char *alerts_level_name(alert_level_t)`
- Task 17 (web app alert editor) mirrors this exact rule semantics.

- [ ] **Step 1: Write the failing tests**

`firmware/test/test_alerts/test_alerts.c`:
```c
#include <math.h>
#include "unity.h"
#include "alerts.h"

void setUp(void) {}
void tearDown(void) {}

static void test_gt_is_strict(void)
{
    alert_rule_t r = { ALERT_OP_GT, 100.0, ALERT_SEVERE };
    TEST_ASSERT_EQUAL_INT(ALERT_SEVERE, alerts_eval(&r, 100.1));
    TEST_ASSERT_EQUAL_INT(ALERT_NONE,   alerts_eval(&r, 100.0));  /* boundary excluded */
}

static void test_gte_includes_boundary(void)
{
    alert_rule_t r = { ALERT_OP_GTE, 100.0, ALERT_SEVERE };
    TEST_ASSERT_EQUAL_INT(ALERT_SEVERE, alerts_eval(&r, 100.0));
}

/* A sensor reporting "unavailable" must never raise a weather alarm (FR-5b). */
static void test_non_finite_never_alarms(void)
{
    alert_rule_t r = { ALERT_OP_LT, 0.0, ALERT_SEVERE };
    TEST_ASSERT_EQUAL_INT(ALERT_NONE, alerts_eval(&r, NAN));
    TEST_ASSERT_EQUAL_INT(ALERT_NONE, alerts_eval(&r, INFINITY));
    TEST_ASSERT_EQUAL_INT(ALERT_NONE, alerts_eval(&r, -INFINITY));
}

static void test_most_severe_wins(void)
{
    alert_rule_t rs[] = {
        { ALERT_OP_GT,  90.0, ALERT_ADVISORY },
        { ALERT_OP_GT, 100.0, ALERT_SEVERE   },
    };
    TEST_ASSERT_EQUAL_INT(ALERT_SEVERE,   alerts_eval_all(rs, 2, 105.0));
    TEST_ASSERT_EQUAL_INT(ALERT_ADVISORY, alerts_eval_all(rs, 2,  95.0));
    TEST_ASSERT_EQUAL_INT(ALERT_NONE,     alerts_eval_all(rs, 2,  50.0));
}

static void test_level_names(void)
{
    TEST_ASSERT_EQUAL_STRING("none",     alerts_level_name(ALERT_NONE));
    TEST_ASSERT_EQUAL_STRING("advisory", alerts_level_name(ALERT_ADVISORY));
    TEST_ASSERT_EQUAL_STRING("warning",  alerts_level_name(ALERT_WARNING));
    TEST_ASSERT_EQUAL_STRING("severe",   alerts_level_name(ALERT_SEVERE));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_gt_is_strict);
    RUN_TEST(test_gte_includes_boundary);
    RUN_TEST(test_non_finite_never_alarms);
    RUN_TEST(test_most_severe_wins);
    RUN_TEST(test_level_names);
    return UNITY_END();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd firmware && pio test -e native -f test_alerts`
Expected: FAIL — `alerts.h` not found.

- [ ] **Step 3: Write the minimal implementation**

`firmware/lib/alerts/include/alerts.h`:
```c
#pragma once

typedef enum { ALERT_NONE = 0, ALERT_ADVISORY, ALERT_WARNING, ALERT_SEVERE } alert_level_t;

typedef enum { ALERT_OP_GT = 0, ALERT_OP_GTE, ALERT_OP_LT, ALERT_OP_LTE,
               ALERT_OP_EQ, ALERT_OP_NE } alert_op_t;

typedef struct {
    alert_op_t    op;
    double        threshold;
    alert_level_t level;
} alert_rule_t;

/* Returns the rule's level when the comparison holds, else ALERT_NONE.
 * A non-finite value (NaN/inf, e.g. a sensor reporting "unavailable")
 * always returns ALERT_NONE — a missing reading must never raise an alarm. */
alert_level_t alerts_eval(const alert_rule_t *rule, double value);

/* Evaluate a list of rules; returns the MOST SEVERE level that fires. */
alert_level_t alerts_eval_all(const alert_rule_t *rules, int n, double value);

/* Map a level to a severity word for display. */
const char *alerts_level_name(alert_level_t level);
```

`firmware/lib/alerts/src/alerts.c`:
```c
#include "alerts.h"
#include <math.h>

static int cmp_fires(alert_op_t op, double v, double t)
{
    switch (op) {
    case ALERT_OP_GT:  return v >  t;
    case ALERT_OP_GTE: return v >= t;
    case ALERT_OP_LT:  return v <  t;
    case ALERT_OP_LTE: return v <= t;
    case ALERT_OP_EQ:  return v == t;
    case ALERT_OP_NE:  return v != t;
    }
    return 0;
}

alert_level_t alerts_eval(const alert_rule_t *rule, double value)
{
    if (!rule) return ALERT_NONE;
    if (!isfinite(value) || !isfinite(rule->threshold)) return ALERT_NONE;
    return cmp_fires(rule->op, value, rule->threshold) ? rule->level : ALERT_NONE;
}

alert_level_t alerts_eval_all(const alert_rule_t *rules, int n, double value)
{
    alert_level_t worst = ALERT_NONE;
    for (int i = 0; i < n; i++) {
        alert_level_t l = alerts_eval(&rules[i], value);
        if (l > worst) worst = l;
    }
    return worst;
}

const char *alerts_level_name(alert_level_t level)
{
    switch (level) {
    case ALERT_ADVISORY: return "advisory";
    case ALERT_WARNING:  return "warning";
    case ALERT_SEVERE:   return "severe";
    default:             return "none";
    }
}
```

`firmware/lib/alerts/library.json`:
```json
{
  "name": "alerts",
  "version": "0.1.0",
  "build": { "srcDir": "src", "includeDir": "include" }
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd firmware && pio test -e native -f test_alerts`
Expected: 5 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/alerts firmware/test/test_alerts
git commit -m "feat(alerts): threshold rule evaluation with non-finite values never alarming"
```

---

### Task 5: VBAT sensing and power-source classification (HW-3, FR-8)

**Files:**
- Create: `firmware/lib/power/library.json`
- Create: `firmware/lib/power/include/power.h`
- Create: `firmware/lib/power/src/power.c`
- Create: `firmware/test/test_power_math/test_power_math.c`

**Interfaces:**
- Produces:
  - `VBAT_DIVIDER_RATIO` (`1M/(300k+1M)` = 0.76923…, from the schematic)
  - `double power_vbat_from_vref(double vref_volts)`
  - `double power_vref_from_raw(int raw, int max_raw, double vref_fullscale)`
  - `typedef enum { POWER_SOURCE_UNKNOWN, POWER_SOURCE_BATTERY, POWER_SOURCE_USB } power_source_t;`
  - `power_source_t power_classify(double vbat_volts, double trend_v_per_min)`

- [ ] **Step 1: Write the failing tests**

`firmware/test/test_power_math/test_power_math.c`:
```c
#include <math.h>
#include "unity.h"
#include "power.h"

void setUp(void) {}
void tearDown(void) {}

/* The divider ratio is a verified schematic fact: R8=300k top, R9=1M bottom. */
static void test_divider_ratio(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.7692307692307693, VBAT_DIVIDER_RATIO);
}

static void test_vbat_recovered_from_node_voltage(void)
{
    /* A 4.2 V battery presents 4.2 * ratio at the sense node... */
    TEST_ASSERT_FLOAT_WITHIN(1e-3, 4.2, power_vbat_from_vref(4.2 * VBAT_DIVIDER_RATIO));
    /* ...so 3.0 V at the node implies ~3.9 V battery. */
    TEST_ASSERT_FLOAT_WITHIN(1e-3, 3.9, power_vbat_from_vref(3.0));
}

static void test_raw_to_volts_uses_denominator_not_max(void)
{
    /* 12-bit ADC: half scale is 2048/4096, NOT 2048/4095. */
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.65, power_vref_from_raw(2048, 4096, 3.3));
}

static void test_classification(void)
{
    TEST_ASSERT_EQUAL_INT(POWER_SOURCE_USB,     power_classify(4.20,  0.00));
    TEST_ASSERT_EQUAL_INT(POWER_SOURCE_USB,     power_classify(4.20,  0.01));
    TEST_ASSERT_EQUAL_INT(POWER_SOURCE_BATTERY, power_classify(4.00, -0.01));
    TEST_ASSERT_EQUAL_INT(POWER_SOURCE_BATTERY, power_classify(3.70,  0.0));
    /* An unreadable ADC must be UNKNOWN, never a guess. */
    TEST_ASSERT_EQUAL_INT(POWER_SOURCE_UNKNOWN, power_classify(NAN, 0.0));
    TEST_ASSERT_EQUAL_INT(POWER_SOURCE_UNKNOWN, power_classify(0.0, 0.0));
}

/* The trend term is what keeps a charged pack from reading as mains: a full cell at
 * 4.2 V that is FALLING is discharging, so it is on battery despite the high voltage.
 * This is the case that a voltage-only rule would get wrong — if you ever "simplify"
 * power_classify to `vbat >= 4.15`, this test is what must stop you. */
static void test_full_charge_but_discharging_is_battery_not_usb(void)
{
    TEST_ASSERT_EQUAL_INT(POWER_SOURCE_BATTERY, power_classify(4.20, -0.02));
    TEST_ASSERT_EQUAL_INT(POWER_SOURCE_BATTERY, power_classify(4.19, -0.05));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_divider_ratio);
    RUN_TEST(test_vbat_recovered_from_node_voltage);
    RUN_TEST(test_raw_to_volts_uses_denominator_not_max);
    RUN_TEST(test_classification);
    RUN_TEST(test_full_charge_but_discharging_is_battery_not_usb);
    return UNITY_END();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd firmware && pio test -e native -f test_power_math`
Expected: FAIL — `power.h` not found.

- [ ] **Step 3: Write the minimal implementation**

`firmware/lib/power/include/power.h`:
```c
#pragma once

/* VBAT sense divider on the ESP32-M1 (verified from the schematic):
 *   VREF = VBAT * R9/(R8+R9) = VBAT * 1M/(300k+1M)
 * R8 = 300k from VBAT, R9 = 1M to GND, landing on GPIO26 (ADC2_CH9).
 * Therefore VBAT = VREF / VBAT_DIVIDER_RATIO. */
#define VBAT_DIVIDER_R_TOP_OHMS    300000.0
#define VBAT_DIVIDER_R_BOTTOM_OHMS 1000000.0
#define VBAT_DIVIDER_RATIO \
    (VBAT_DIVIDER_R_BOTTOM_OHMS / (VBAT_DIVIDER_R_TOP_OHMS + VBAT_DIVIDER_R_BOTTOM_OHMS))

/* Convert a measured voltage at the VREF node to actual battery volts. */
double power_vbat_from_vref(double vref_volts);

/* Linear-map a raw ADC reading to volts given the ADC full-scale reference. */
double power_vref_from_raw(int raw, int max_raw, double vref_fullscale);

typedef enum {
    POWER_SOURCE_UNKNOWN = 0,
    POWER_SOURCE_BATTERY,
    POWER_SOURCE_USB
} power_source_t;

/* Classify the supply. Thresholds are Li-ion-typical and MUST be calibrated on
 * real hardware (spec Q7): a charging cell sits above ~4.15 V, and only the USB
 * path can hold it there. trend_v_per_min > 0 means voltage is rising.
 *
 * HARDWARE LIMIT (verified from the schematic): there is NO confirmed USB-present or
 * charge-status pin on this board — a VBUS net and the LTC4054's CHRG output exist, but
 * neither destination GPIO resolves — so the mode is INFERRED from VBAT alone and can be
 * wrong. The config therefore carries a user `powerMode` override
 * ('auto' | 'always-on' | 'battery') so a bad guess is correctable (FR-8:
 * "user-selectable / auto-detected"). The threshold constant is to be calibrated on real
 * hardware in Step 4; the values here are Li-ion-typical, not measured.
 *
 * Note the trend term is what disambiguates a charged pack from mains: `trend >= 0`
 * means "not falling", so a full cell that is actually DISCHARGING (high voltage, falling
 * trend) is classified BATTERY, not USB. Do not "simplify" this to a voltage-only test —
 * that is the one change that would let an unplugged full pack read as mains. */
power_source_t power_classify(double vbat_volts, double trend_v_per_min);
```

`firmware/lib/power/src/power.c`:
```c
#include "power.h"
#include <math.h>

double power_vbat_from_vref(double vref_volts)
{
    if (!isfinite(vref_volts)) return 0.0;
    return vref_volts / VBAT_DIVIDER_RATIO;
}

double power_vref_from_raw(int raw, int max_raw, double vref_fullscale)
{
    if (max_raw <= 0) return 0.0;
    return ((double)raw / (double)max_raw) * vref_fullscale;
}

power_source_t power_classify(double vbat_volts, double trend_v_per_min)
{
    if (!isfinite(vbat_volts) || vbat_volts <= 0.0) return POWER_SOURCE_UNKNOWN;
    if (vbat_volts >= 4.15 && trend_v_per_min >= 0.0) return POWER_SOURCE_USB;
    return POWER_SOURCE_BATTERY;
}
```

`firmware/lib/power/library.json`:
```json
{
  "name": "power",
  "version": "0.1.0",
  "build": { "srcDir": "src", "includeDir": "include" }
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd firmware && pio test -e native -f test_power_math`
Expected: 5 tests PASS.

**Then calibrate the threshold on real hardware (do NOT skip).** The 4.15 V value is
Li-ion-typical, not measured on this board. With the device on a bench supply or a real
cell, record the VREF-derived VBAT at: (a) unplugged and resting after a full charge,
(b) plugged into USB, (c) mid-discharge. If (a) is within 0.05 V of the threshold, widen
the gap. Note the three measurements in the commit message. **Also confirm the `powerMode`
override actually forces the mode** — it is the user's escape hatch when auto-detect is
wrong (FR-8), and with no confirmed USB-present pin there is no other backstop.

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/power firmware/test/test_power_math
git commit -m "feat(power): VBAT divider math and battery/USB classification"
```

---

## Phase 2 — Hardware bring-up (needs the board)

Everything before this phase is provable on the host. From here on each task is verified
on the **real ESP32-M1 + panel**, because the vendor register behaviour cannot be
simulated (HW-5) and the SPI timing is unpublished (HW-7).

### Task 6: EPD driver core — vendor register sequence + 1→2bpp, with a host-testable encoder

**Files:**
- **Pure, host-tested:** the 1 bpp → 2 bpp expansion and LUT selection (no hardware).
  - Create: `firmware/lib/layout/include/epd_encode.h`
  - Create: `firmware/lib/layout/src/epd_encode.c`
  - Create: `firmware/test/test_epd_encode/test_epd_encode.c`
- **Hardware (IDF component, on-device only):**
  - Create: `firmware/components/epd/CMakeLists.txt`
  - Create: `firmware/components/epd/include/epd.h`
  - Create: `firmware/components/epd/epd.c`
  - Create: `firmware/components/epd/epd_spi.c`

**Interfaces:**
- Consumes: `canvas.h` (Task 3).
- Produces (pure): `epd_expand_1to2(const uint8_t *in1bpp, size_t n_bytes, uint8_t *out2bpp)`
  — the vendor demo's exact expansion; `epd_interleave_1to2(const uint8_t *prev1bpp,
  const uint8_t *next1bpp, size_t n_bytes, uint8_t *out2bpp)`; and
  `epd_lut_value_for_temp(int temp_c) -> int`.
- Produces (hardware): `epd_init(void)`, `epd_write_frame(const uint8_t *fb1bpp)`,
  `epd_write_frame_partial(const uint8_t *prev1bpp, const uint8_t *next1bpp)`,
  `epd_sleep(void)`, `epd_read_temp(int *out_c)`, `epd_wait_ready(void)`.

> **[VERIFIED 2026-09-18 — the partial path is NOT the same as the full path.]** The vendor
> demo's partial update (`PIC_display_Part_ALL`, `Display_EPD_W21.cpp:369`) takes **two**
> framebuffers and calls `bitInterleave(prev, next)` per byte pair — it never calls
> `EPD_W21_WriteDATA_1To2()`. The SSD2677 needs the **previously displayed** frame to
> derive each pixel's transition. So `epd_write_frame_partial` MUST take `(prev, next)`, and
> the caller must track what is currently on the glass. The encoder therefore needs **two**
> functions: `epd_expand_1to2` (full) and `epd_interleave_1to2` (partial). An earlier draft
> of this plan specified a single-argument partial writer — that was wrong and would have
> sent a frame the controller cannot interpret. Both functions are verified exhaustively
> against the vendor reference (256 byte values and 65,536 byte pairs, 0 mismatches).
- **BUSY contract (all hardware functions return `esp_err_t`):** `epd_wait_ready()` is
  **bounded** (20 s) and returns `ESP_ERR_TIMEOUT`; the frame/sleep/temp functions
  propagate it. The vendor demo's `lcd_chkstatus()` is an unbounded `while(1)` — copying
  that would hang the device forever on a loose FPC or an unpowered panel, with no
  watchdog and no visible error (violates FR-29). Never port an unbounded wait.

- [ ] **Step 1: Write the failing tests for the pure encoder**

The 1→2 bpp expansion is the vendor demo's `EPD_W21_WriteDATA_1To2()`: the demo's inner
loop walks 4 bits at a time and emits **one byte per nibble**, so **one input byte becomes
exactly two output bytes**. *(This was derived by simulating the vendor function over all
256 byte values, not by eye — an earlier hand-derivation of the bit positions was wrong and
the test caught it.)*

`firmware/test/test_epd_encode/test_epd_encode.c`:
```c
#include "unity.h"
#include "epd_encode.h"

void setUp(void) {}
void tearDown(void) {}

/* 1 input byte -> exactly 2 output bytes. Values confirmed against the vendor demo. */
static void test_expand_matches_vendor_reference(void)
{
    uint8_t in[3] = { 0x00, 0xFF, 0xA0 };
    uint8_t o[6]  = { 9, 9, 9, 9, 9, 9 };
    epd_expand_1to2(in, 3, o);
    TEST_ASSERT_EQUAL_UINT8(0x00, o[0]);  TEST_ASSERT_EQUAL_UINT8(0x00, o[1]); /* 0x00 */
    TEST_ASSERT_EQUAL_UINT8(0xFF, o[2]);  TEST_ASSERT_EQUAL_UINT8(0xFF, o[3]); /* 0xFF */
    TEST_ASSERT_EQUAL_UINT8(0xCC, o[4]);  TEST_ASSERT_EQUAL_UINT8(0x00, o[5]); /* 0xA0 */
}

/* Each nibble expands independently: 0xF0 -> FF 00 ; 0x0F -> 00 FF */
static void test_nibble_expansion(void)
{
    uint8_t f0[1] = { 0xF0 }, of[2];
    epd_expand_1to2(f0, 1, of);
    TEST_ASSERT_EQUAL_UINT8(0xFF, of[0]); TEST_ASSERT_EQUAL_UINT8(0x00, of[1]);

    uint8_t f[1] = { 0x0F }, o0[2];
    epd_expand_1to2(f, 1, o0);
    TEST_ASSERT_EQUAL_UINT8(0x00, o0[0]); TEST_ASSERT_EQUAL_UINT8(0xFF, o0[1]);
}

/* Temperature -> LUT value, taken verbatim from the vendor demo's Write_LUT_All(). */
static void test_lut_selection_matches_vendor_demo(void)
{
    TEST_ASSERT_EQUAL_INT(232, epd_lut_value_for_temp(-20));
    TEST_ASSERT_EQUAL_INT(232, epd_lut_value_for_temp(5));
    TEST_ASSERT_EQUAL_INT(235, epd_lut_value_for_temp(10));
    TEST_ASSERT_EQUAL_INT(238, epd_lut_value_for_temp(20));
    TEST_ASSERT_EQUAL_INT(241, epd_lut_value_for_temp(30));
    TEST_ASSERT_EQUAL_INT(244, epd_lut_value_for_temp(40));
    TEST_ASSERT_EQUAL_INT(232, epd_lut_value_for_temp(200));  /* out of range -> default */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_expand_matches_vendor_reference);
    RUN_TEST(test_nibble_expansion);
    RUN_TEST(test_lut_selection_matches_vendor_demo);
    return UNITY_END();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd firmware && pio test -e native -f test_epd_encode`
Expected: FAIL — `epd_encode.h` not found.

- [ ] **Step 3: Write the minimal implementation**

`firmware/lib/layout/include/epd_encode.h`:
```c
#pragma once
#include <stddef.h>
#include <stdint.h>

/* Expand 1 bpp framebuffer data to the SSD2677's 2 bpp (4-grey) wire format,
 * matching the vendor demo's EPD_W21_WriteDATA_1To2() exactly. One input byte
 * becomes exactly TWO output bytes (high nibble first, then low).
 * `out` MUST have room for 2 * n_bytes. */
void epd_expand_1to2(const uint8_t *in, size_t n_bytes, uint8_t *out);

/* Temperature-compensated waveform selector, verbatim from the vendor demo's
 * Write_LUT_All(). Values are the raw bytes written to register 0xE6. */
int epd_lut_value_for_temp(int temp_c);
```

`firmware/lib/layout/src/epd_encode.c`:
```c
#include "epd_encode.h"

/* One input nibble -> ONE output byte. Nibble bit (3-b) sets output bits
 * (2*(3-b)) and (2*(3-b)+1), reproducing the vendor demo's inner loop:
 *   for k in 0..3: if MSB set, temp3 |= 0x03; if k<=2, temp3 <<= 2
 * Verified exhaustively against the vendor reference for all 256 byte values. */
static uint8_t emit_nibble(uint8_t n)
{
    uint8_t r = 0;
    for (int b = 0; b < 4; b++) {
        if (n & (0x8u >> b)) {
            r |= (uint8_t)(0x3u << (2 * (3 - b)));
        }
    }
    return r;
}

void epd_expand_1to2(const uint8_t *in, size_t n_bytes, uint8_t *out)
{
    for (size_t i = 0; i < n_bytes; i++) {
        out[2 * i]     = emit_nibble((uint8_t)((in[i] >> 4) & 0x0F));
        out[2 * i + 1] = emit_nibble((uint8_t)(in[i] & 0x0F));
    }
}

int epd_lut_value_for_temp(int temp_c)
{
    if (temp_c <= 5)   return 232;
    if (temp_c <= 10)  return 235;
    if (temp_c <= 20)  return 238;
    if (temp_c <= 30)  return 241;
    if (temp_c <= 127) return 244;
    return 232;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd firmware && pio test -e native -f test_epd_encode`
Expected: 3 tests PASS.

- [ ] **Step 5: Write the hardware driver (IDF component, vendor sequence verbatim)**

`firmware/components/epd/include/epd.h`:
```c
#pragma once
#include <stdint.h>

/* Pin map verified from the schematic (HW-2 / spec §2.3). */
#define EPD_PIN_BUSY  13
#define EPD_PIN_RES   12
#define EPD_PIN_DC    14
#define EPD_PIN_CS    27
#define EPD_PIN_SCLK  18
#define EPD_PIN_SDI   23

/* Every hardware entry point returns esp_err_t. BUSY waits are BOUNDED (see epd.c):
 * ESP_ERR_TIMEOUT means the panel did not release BUSY in time — a loose FPC, an
 * unpowered panel, or a stalled controller. Callers must surface this and keep the
 * last good image (FR-29); they must never spin forever. */
esp_err_t epd_init(void);                                  /* full-update init (HW-5) */
esp_err_t epd_init_partial(void);                          /* partial-OTP init */
esp_err_t epd_write_frame(const uint8_t *fb1bpp);          /* full update */
/* Partial update takes the frame CURRENTLY ON THE GLASS plus the new frame; the
 * controller derives each pixel's transition from the pair (vendor PIC_display_Part_ALL). */
esp_err_t epd_write_frame_partial(const uint8_t *prev1bpp, const uint8_t *next1bpp);
esp_err_t epd_sleep(void);                                 /* power-off + deep sleep (FR-12) */
esp_err_t epd_read_temp(int *out_c);                       /* cmd 0x40, returns degC */
esp_err_t epd_wait_ready(void);                            /* bounded BUSY wait (FR-13) */
esp_err_t epd_wait_ready_ms(int timeout_ms);               /* explicit timeout */
```

`firmware/components/epd/epd.c`:
```c
#include "epd.h"
#include "epd_encode.h"
#include "canvas.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

/* The register sequence below is VENDOR-PROPRIETARY (HW-5): registers 0x00 PSR,
 * 0x30, 0x62 HTOTAL, 0x65 GSST, 0xE0, 0xE6, 0xE7, 0xE9 do NOT appear in the
 * public SSD2677 datasheet. Reproduced verbatim from
 * GDEH0576T81_Arduino_demo_code/Display_EPD_W21.cpp. DO NOT "clean this up". */

/* BUSY is active-low on this panel (demo: `if (isEPD_W21_BUSY==1) break;` — it breaks
 * when BUSY reads HIGH). The vendor demo waits in an unbounded `while(1)`. We must NOT:
 * a loose FPC or unpowered panel would hang the device forever, with no watchdog and no
 * error surfaced (FR-29 requires the failure be visible, not a wedged panel). Bound it.
 *
 * Measure the bound against the WALL CLOCK, never a loop-iteration count. An earlier
 * version of this file counted iterations with `vTaskDelay(pdMS_TO_TICKS(1))`; at the
 * default CONFIG_FREERTOS_HZ=100 that is ZERO ticks, so the delay never blocked and a
 * nominal 20 s bound expired in ~228 ms — on a HEALTHY panel, mid-refresh. Every refresh
 * reported ESP_ERR_TIMEOUT and nothing was drawn. Verified on hardware 2026-09-18. */
esp_err_t epd_wait_ready_ms(int timeout_ms)
{
    if (timeout_ms < 0) return ESP_ERR_INVALID_ARG;
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (epd_spi_busy_read() != 1) {          /* 1 = released/idle */
        if (esp_timer_get_time() >= deadline) return ESP_ERR_TIMEOUT;
        vTaskDelay(1);                          /* exactly one tick; never rounds to 0 */
    }
    return ESP_OK;
}

/* Generous: a full refresh is ~2-4 s, so 20 s cannot trip on a healthy panel but still
 * fails fast enough to stay inside the boot path's error budget. */
esp_err_t epd_wait_ready(void) { return epd_wait_ready_ms(20000); }

static void delay_ms(int ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

static void write_lut_all(void)
{
    int t = 0;
    (void)epd_read_temp(&t);
    epd_write_cmd(0xE0);
    epd_write_data(0x02);
    epd_write_cmd(0xE6);
    epd_write_data((uint8_t)epd_lut_value_for_temp(t));
    epd_write_cmd(0xA5);
    epd_wait_ready();
    delay_ms(10);
}

esp_err_t epd_init(void)
{
    delay_ms(10);
    epd_spi_reset(0); delay_ms(10);
    epd_spi_reset(1); delay_ms(10);
    /* NOTE: unlike the vendor demo, do NOT `return` early on timeout here. The vendor
     * sequence must still be written in full, because a partially-initialised controller
     * may latch undefined state. Record the first failure and keep going; the caller
     * decides (FR-29: show the error, keep the last good image). */
    esp_err_t first_err = epd_wait_ready();

    epd_write_cmd(0x00); epd_write_data(0x27); epd_write_data(0x0E);
    epd_wait_ready();
    epd_write_cmd(0x06);
    epd_write_data(0x0F); epd_write_data(0x8B);
    epd_write_data(0x9C); epd_write_data(0xC1);
    epd_write_cmd(0xE7); epd_write_data(0xC1);
    epd_write_cmd(0x30); epd_write_data(0x08);
    epd_write_cmd(0x50); epd_write_data(0x77);
    epd_write_cmd(0x61);
    epd_write_data(EPD_WIDTH / 256);  epd_write_data(EPD_WIDTH % 256);
    epd_write_data(EPD_HEIGHT / 256); epd_write_data(EPD_HEIGHT % 256);
    epd_write_cmd(0x62);
    static const uint8_t htotal[8] = { 0x98,0x98,0x98,0x75,0xCA,0xB2,0x98,0x7E };
    for (int i = 0; i < 8; i++) epd_write_data(htotal[i]);
    epd_write_cmd(0x65);
    epd_write_data(0x00); epd_write_data(0x00);
    epd_write_data(0x00); epd_write_data(0x00);
    epd_write_cmd(0xE9); epd_write_data(0x01);
    write_lut_all();
    epd_write_cmd(0x04);           /* power on */
    if (epd_wait_ready() != ESP_OK && first_err == ESP_OK) first_err = ESP_ERR_TIMEOUT;
    return first_err;
}

/* Returns ESP_OK only if the whole frame — including the refresh completing — succeeded.
 * On timeout the panel is left alone rather than retried, so the previous image stays. */
esp_err_t epd_write_frame(const uint8_t *fb1bpp)
{
    static uint8_t line2[EPD_PITCH * 2];   /* one 2bpp row, 230 bytes */
    epd_write_cmd(0x10);
    if (epd_wait_ready() != ESP_OK) return ESP_ERR_TIMEOUT;
    for (int y = 0; y < EPD_HEIGHT; y++) {
        epd_expand_1to2(&fb1bpp[(size_t)y * EPD_PITCH], EPD_PITCH, line2);
        epd_write_data_block(line2, sizeof(line2));
    }
    epd_write_cmd(0x12);           /* DRF: display refresh */
    epd_write_data(0x00);
    return epd_wait_ready();       /* ~2-4 s of actual e-paper update happens here */
}

esp_err_t epd_sleep(void)
{
    epd_write_cmd(0x02); epd_write_data(0x00);   /* power off */
    esp_err_t err = epd_wait_ready();
    epd_write_cmd(0x07); epd_write_data(0xA5);   /* deep sleep (FR-12) */
    return err;
}

esp_err_t epd_read_temp(int *out_c)
{
    epd_write_cmd(0x40);
    if (epd_wait_ready() != ESP_OK) return ESP_ERR_TIMEOUT;
    int raw = epd_spi_read_byte();     /* datasheet: returns degC directly */
    if (raw > 127) raw -= 256;         /* two's complement */
    *out_c = raw;
    return ESP_OK;
}
```
> **Implementer note:** `epd_init_partial()`, `epd_write_frame_partial()` and the
> `epd_spi_*` transport are mechanical ports of the same vendor file
> (`EPD_init_Part()`, `PIC_display_Part_ALL()`, `Display_EPD_W21_spi.cpp`). Port them
> verbatim in the same style. **Start with software SPI exactly as the demo does** — do
> not optimise to hardware SPI in this task (HW-7); that is Task 6c.

- [ ] **Step 6: Build for the device and prove the panel shows the vendor reference image**

Write a temporary `app_main` that initialises the panel and writes a full-black frame, then
a full-white frame, then the vendor demo's `gImage_1` (copy it from
`GDEH0576T81_Arduino_demo_code/Ap_29demo.h` into a test fixture during bring-up only).

```bash
cd firmware && pio run -e esp32dev -t upload && pio device monitor
```
Expected (on hardware): full-black, full-white, then `gImage_1` render correctly, with
no stuck BUSY and no watchdog reset. **This is the regression baseline for HW-5: the
output must be indistinguishable from the vendor demo.**

**a) Prove the BUSY timeout actually fires — deterministically, WITHOUT unplugging the panel FPC.**

> **Why not just unplug the FPC:** the vendor demo configures BUSY as
> `pinMode(A14, INPUT)` — a **plain input with no pull-up** — and the firmware matches it.
> An absent panel therefore leaves GPIO13 **floating**, sitting at an indeterminate level
> that can read HIGH (appearing "ready"). So an unplug test is not just fiddly, it is
> **non-deterministic**: it may pass even if the guard is broken. Worse, pulling a
> 24-pin 0.5 mm FPC by hand repeatedly risks damaging the connector or the panel.
> Do not test this by unplugging.

Test it instead in a way that is deterministic, repeatable, and electrically safe:

**a1) Fast regression test — short-timeout injection (no hardware change, no risk).**
The bounded wait is parameterised (`epd_wait_ready_ms`), so exercise it while the panel
is **genuinely busy** — which is the state a loose FPC or stalled controller produces.

> **Correction (verified on hardware 2026-09-18):** the first draft of this step said
> "call `epd_wait_ready_ms(1)` — a refresh holds BUSY low for seconds, so a 1 ms bound
> must expire." That is only true *during* a refresh. At rest a healthy panel has BUSY
> **released (high)**, so `epd_wait_ready_ms(1)` returns `ESP_OK` immediately and proves
> nothing about the bound. The test must first drive the panel into the busy state.

```c
/* Start a real refresh, then catch BUSY while it is held low. */
epd_write_cmd(0x10);
assert(epd_wait_ready() == ESP_OK);
/* ...write a frame's worth of data... */
epd_write_cmd(0x12); epd_write_data(0x00);   /* DRF: panel is now BUSY for seconds */

int64_t t0 = esp_timer_get_time();
esp_err_t e = epd_wait_ready_ms(1);
assert(e == ESP_ERR_TIMEOUT);                 /* the bound fires */
assert(esp_timer_get_time() - t0 < 100000);   /* ...promptly, in ~5 ms */
/* and the device must still be alive and finish the refresh normally */
assert(epd_wait_ready() == ESP_OK);
```
Expected: returns `ESP_ERR_TIMEOUT` promptly, **no hang, no watchdog reset, no reboot
loop**, and the serial log names the timeout. Confirm the device is still reachable
after the failure. This must be repeatable.

**Measured 2026-09-18 on the ESP32-M1 + GDEH0576T81:** bound expired in **5 ms**;
the subsequent unbounded wait completed `ESP_OK` in **919 ms**; the device stayed alive.
Wire this into a `test_epd_busy` suite as a standing regression — it needs only the
board, not the panel.

**a2) Full end-to-end test on a separate ESP32-DevKitC (optional, recommended once).**
This proves the real 20 s bound and the real caller path without touching the M1 or its
FPC. On a DevKitC, wire **GPIO13 to GND** with a jumper and run this firmware. Because the
firmware reads BUSY as a plain input, tying it low pins the "stuck busy" condition
**deterministically** — the exact thing a floating unplug test cannot guarantee.
Expected: `epd_init()` returns `ESP_ERR_TIMEOUT` after ~20 s; the device **stays up**,
logs the timeout, and does **not** reboot-loop. (Do *not* use a pull-**up**; that reads as
"ready" and tests nothing.) GPIO13 is a plain GPIO configured as input, so grounding it
risks no contention.

**a3) Confirm the 20 s bound has real margin.** From step 6b, a full refresh must measure
well under 20 s (expect ~2–4 s). If any measured wait approaches the bound, raise the
bound — a timeout that can fire on a *healthy* panel would turn normal operation into a
visible error. Record the measured refresh time next to the bound so the margin is explicit.

**a4) Graceful-degradation check (FR-29).** With the BUSY failure forced by a1, confirm the
device still brings up WiFi and answers `GET /api/status` **with an error field set**, and
that it keeps the last good image rather than blanking. A stuck panel must cost you the
display, not the device.

A hang in any of these means the unbounded wait crept back in. This is the single most
important safety check in the task — it is the difference between *"panel unplugged, device
still reachable over WiFi to tell you"* and *"device bricked on a wall"*.

**b) Measure both refresh waveforms on real hardware.** Instrument with
`esp_timer_get_time()` around the DRF (0x12) wait and log the elapsed time:
- **Full refresh** (`epd_write_frame`): expect roughly **2–4 s**. Note the measured value.
- **Partial refresh** (`epd_write_frame_partial`): expect **well under 1 s**.
- **Deep sleep**, then a re-init + full refresh: confirm the panel wakes and redraws
  correctly, because FR-12 requires sleeping between every update.

**Measured 2026-09-18 (ESP32-M1 + GDEH0576T81, software SPI, 160 MHz):**

| Operation | Measured | Bound |
|---|---|---|
| Full refresh, all-black | **2379 ms** | 20 s (≈8× margin) |
| Full refresh, all-white | **2369 ms** | 20 s |
| Full refresh, vendor `gImage_1` | **2369 ms** | 20 s |
| `epd_wait_ready()` mid-refresh | 919 ms | 20 s |
| **Partial refresh** | **1867 ms** | 20 s |
| Deep sleep | < 1 ms | — |

All three full refreshes landed within 10 ms of each other, so the waveform is
consistent. **The 20 s bound holds with ~8× margin — do not lower it.**

> **[VERIFIED 2026-09-18 — two surprises in the partial path.]**
> 1. **Partial is NOT "well under 1 s"; it measured 1867 ms** (only ~21% faster than a
>    full refresh). The earlier "well under 1 s" figure in this plan was an assumption
>    and it was wrong.
> 2. **"Partial" means a different WAVEFORM, not a smaller region.** The vendor's
>    `PIC_display_Part_ALL()` writes all 680 rows — the same 78,200-byte payload as a
>    full refresh. The saving is the waveform (no flashing/ghosting-clear cycle), not
>    the data. Do not design FR-11 around the idea that a partial update transfers less.
>
> **Both costs are dominated by software bit-banged SPI**, which moves 156,400 wire
> bytes per frame at ~2 GPIO writes per bit. This was the strongest argument for the
> hardware-SPI migration, which **Task 6c has since completed and verified**: push
> 1468 ms -> 233 ms, full refresh 2389 ms -> 1289 ms, leaving the ~1056 ms waveform as
> the floor. See Task 6c for the measurements and the three silent-failure traps.
>
> Ghosting (step d): after **8** consecutive partials over a mid-grey field the logo
> stayed crisp and the changed blocks were clean — no gross ghosting visible at the
> camera's resolution. FR-11's `[ASSUMED 5]` default is therefore **not** contradicted,
> but 8 is not enough evidence to raise it, and the vendor's own demo still full-refreshes
> every 5. Leave the default at 5 and revisit in Task 12 with a longer run.

These numbers are the input to the update-interval and deep-sleep duty-cycle decisions in
Task 12 — and therefore to battery life (FR-8). Record them; do not guess them.

**c) Confirm the customer-facing behaviour the user asked for.** The panel must look
**fast and good**: a partial update should complete with no visible full-screen flash,
and a full refresh is allowed to flash once (the demo's own README says *"Flickering is
normal when EPD is performing a full screen update to clear ghosting"*).

**d) Confirm the max-partials-before-full-refresh number empirically.** The vendor demo
does a full update after every 5 partials (its own comment; spec FR-11 marks the default
`[ASSUMED 5]`). Render a mid-grey field of changing text through 10+ consecutive partials
and check for residual ghosting at each step. **If ghosting is visible before 5, lower the
default and update FR-11's value in the spec** — an assumed number that causes visible
artifacts is a defect, not a default. Camera captures are adequate for gross ghosting;
for a marginal case, ask the user to eyeball it.

- [ ] **Step 7: Commit**

```bash
git add firmware/lib/layout/include/epd_encode.h firmware/lib/layout/src/epd_encode.c \
        firmware/test/test_epd_encode firmware/components/epd
git commit -m "feat(epd): vendor-verbatim SSD2677 init, 1->2bpp encode, panel bring-up"
```

---

### Task 6b: GT30 font-chip probe and cold-temperature guard (HW-4, HW-1, FR-13)

Both requirements are about **not trusting an assumption**: that the "reserve" font chip is
populated, and that the panel is within its operating range.

**Files:**
- Create: `firmware/components/epd/include/gt30.h`
- Create: `firmware/components/epd/gt30.c`
- Create: `firmware/lib/layout/include/thermal_guard.h`
- Create: `firmware/lib/layout/src/thermal_guard.c`
- Create: `firmware/test/test_thermal_guard/test_thermal_guard.c`

**Interfaces:**
- Produces (device): `int gt30_present(void)` — probes once, caches the result; returns 0
  when the chip does not answer, and **every caller must treat 0 as normal**.
- Produces (pure, host-tested):
  `typedef enum { THERMAL_OK, THERMAL_TOO_COLD, THERMAL_TOO_HOT } thermal_state_t;`
  `thermal_state_t thermal_check(int panel_temp_c, int temp_valid);`

- [ ] **Step 1: Write the failing test for the thermal guard (HW-1)**

The panel datasheet's operating range is **0…50 °C**; the device lives outdoors, so the
cold end is a real risk, not a theoretical one.

`firmware/test/test_thermal_guard/test_thermal_guard.c`:
```c
#include "unity.h"
#include "thermal_guard.h"

void setUp(void) {}
void tearDown(void) {}

/* Documented panel operating range is 0..50 degC (HW-1). */
static void test_in_range_is_ok(void)
{
    TEST_ASSERT_EQUAL_INT(THERMAL_OK,       thermal_check(0, 1));
    TEST_ASSERT_EQUAL_INT(THERMAL_OK,       thermal_check(22, 1));
    TEST_ASSERT_EQUAL_INT(THERMAL_OK,       thermal_check(50, 1));
}

static void test_below_range_is_flagged(void)
{
    TEST_ASSERT_EQUAL_INT(THERMAL_TOO_COLD, thermal_check(-1, 1));
    TEST_ASSERT_EQUAL_INT(THERMAL_TOO_COLD, thermal_check(-20, 1));
}

static void test_above_range_is_flagged(void)
{
    TEST_ASSERT_EQUAL_INT(THERMAL_TOO_HOT,  thermal_check(51, 1));
    TEST_ASSERT_EQUAL_INT(THERMAL_TOO_HOT,  thermal_check(70, 1));
}

/* An unreadable sensor is NOT permission to render, but it is also NOT evidence of
 * cold. On this hardware the SSD2677 returns a constant, so folding "unusable" into
 * TOO_COLD would block every render forever. Unusable -> UNKNOWN -> render and log. */
static void test_unusable_sensor_is_unknown_not_cold(void)
{
    TEST_ASSERT_EQUAL_INT(THERMAL_UNKNOWN, thermal_check(-15, 0));
    TEST_ASSERT_EQUAL_INT(THERMAL_UNKNOWN, thermal_check(20,  0));
    TEST_ASSERT_NOT_EQUAL(THERMAL_TOO_COLD, thermal_check(-15, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_in_range_is_ok);
    RUN_TEST(test_below_range_is_flagged);
    RUN_TEST(test_above_range_is_flagged);
    RUN_TEST(test_unusable_sensor_is_unknown_not_cold);
    return UNITY_END();
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cd firmware && pio test -e native -f test_thermal_guard`
Expected: FAIL — `thermal_guard.h` not found.

- [ ] **Step 3: Implement the thermal guard**

`firmware/lib/layout/include/thermal_guard.h`:
```c
#pragma once

/* Panel datasheet TOPR is 0..50 degC. See the spec's HW-1 note: on this hardware the
 * SSD2677's internal sensor returns a CONSTANT (-15 degC at 0x40), so an unusable
 * reading must be its own state, never TOO_COLD. */
#define PANEL_TEMP_MIN_C 0
#define PANEL_TEMP_MAX_C 50
#define PANEL_TEMP_SENTINEL_C (-128)

typedef enum {
    THERMAL_OK = 0,
    THERMAL_TOO_COLD,
    THERMAL_TOO_HOT,
    THERMAL_UNKNOWN     /* no usable source — do not block on this */
} thermal_state_t;

thermal_state_t thermal_check(int panel_temp_c, int temp_valid);
```

`firmware/lib/layout/src/thermal_guard.c`:
```c
#include "thermal_guard.h"

thermal_state_t thermal_check(int panel_temp_c, int temp_valid)
{
    if (!temp_valid) return THERMAL_UNKNOWN;      /* never guess from a missing sensor */
    if (panel_temp_c <= PANEL_TEMP_SENTINEL_C) return THERMAL_UNKNOWN;
    if (panel_temp_c < PANEL_TEMP_MIN_C)       return THERMAL_TOO_COLD;
    if (panel_temp_c > PANEL_TEMP_MAX_C)       return THERMAL_TOO_HOT;
    return THERMAL_OK;
}
```

- [ ] **Step 4: Run to verify pass**

Run: `cd firmware && pio test -e native -f test_thermal_guard`
Expected: 4 tests PASS.

- [ ] **Step 5: Implement the GT30 probe**

`firmware/components/epd/gt30.c` — **must**:
- read a **known glyph** over the shared SPI bus (HW-2: e-paper CS held HIGH, GT30 CS low),
  with a **short timeout**. The chip has **no read-ID command** — its whole command set is
  `0x03` (read) and `0x0B` (fast read) — so presence is established by reading the 8×16
  ASCII glyph for `'A'` at `GT30_ADDR_8X16_ASCII + ('A'-0x20)*16` and comparing it
  byte-for-byte with LibDriver's reference buffer. That is exactly what LibDriver's own
  `gt30l32s4w_init()` self-test does. A floating MISO cannot reproduce a real glyph.
- call `epd_spi_init()` first. The clock/data lines belong to the e-paper transport;
  without configuring them, a probe that runs *before* `epd_init()` sees SCLK floating and
  reports the chip **absent** even when it is present (observed on hardware).
- cache the result in a static, so it is probed once per boot;
- return 0 on any failure, and never block, never retry in a loop;
- log the outcome once at INFO.

**GT30 CS = GPIO15** — resolved from the schematic (spec Q6): the `SPI_CS_GT30` net lands
on the module's `A13`/`T3` pin. **Data returns on SPI_MISO = GPIO19**, which the sheet
wires to the GT30's pin 2 (`SO`).

**[VERIFIED ON HARDWARE 2026-09-18]** Despite the schematic's `(Reserve)` marking the chip
**is populated**: the probe reads the `'A'` glyph correctly from `0x1DD990` and returns 1.

- [ ] **Step 6: Wire both into the boot/render path**

In `app_boot.c` (Task 12), after `epd_init()`: read the panel temperature, decide whether
it is usable, and call `thermal_check(c, valid)`. **Only `THERMAL_TOO_COLD` / `TOO_HOT`
skip the refresh** and render a clear message instead of a possibly-garbled dashboard
(HW-1); `THERMAL_UNKNOWN` **renders normally and logs** — do not block on it. Then sleep
as usual. Log `gt30_present()` once.

**[MEASURED 2026-09-18]** On this unit the temperature is *always* unusable: `0x40`
returns a constant −15 °C and `0x43` a constant −1 °C, with the TSE offset register
(`0x41`) having no effect on either. The boot path must therefore detect the known
constant and pass `valid = 0`; treating it as a real −15 °C reading would skip every
render. Until a live source exists (external I²C sensor, or a different unit), the guard
is effectively a no-op that logs — which is the honest behaviour, not a failure.

- [ ] **Step 7: On-hardware verification**

- Confirm the boot path **renders** despite the unusable sensor, and that the log names
  the reason. **[DONE 2026-09-18: renders, logs "known-stuck constant - unusable".]**
- Confirm the guard still blocks when genuinely out of range — feed `thermal_check` a
  cold/hot value with `valid = 1` and check it returns `TOO_COLD`/`TOO_HOT`.
  **[DONE: −10 → 1, 55 → 2.]**
- Confirm the log reports the GT30 probe result; **either** result is a pass, as long as it
  does not crash and the device keeps rendering with the flash atlas.
  **[DONE: reports present, glyph verified.]**
- The freezer test is **not currently meaningful** — the sensor does not track ambient, so
  it cannot exercise the cold path. Re-run it only once a live source exists.

- [ ] **Step 8: Commit**

```bash
git add firmware/components/epd/include/gt30.h firmware/components/epd/gt30.c \
        firmware/lib/layout/include/thermal_guard.h firmware/lib/layout/src/thermal_guard.c \
        firmware/test/test_thermal_guard firmware/components/app
git commit -m "feat(epd): GT30 presence probe and panel operating-temperature guard"
```

---

### Task 6c: Hardware-SPI transport migration (HW-7) — COMPLETED 2026-09-18

**Status: implemented and verified on hardware.** This task was missing from the original
plan even though two later tasks referenced "Task 7's hardware-SPI migration" (Task 7 is
OWM parsing). Inserted here so those references resolve.

**Files:** `firmware/components/epd/epd_hw_spi.c`,
`firmware/components/epd/include/epd_hw_spi.h`, `firmware/components/epd/epd_spi.c`,
`firmware/components/epd/epd.c`

**Measured result (same panel, same frame):**

| Operation | Software (bit-bang) | Hardware SPI @10 MHz |
|---|---|---|
| Frame push (156,400 wire bytes) | 1468 ms | **233 ms** |
| Full refresh total | 2389 ms | **1289 ms** |
| Partial refresh total | 1867 ms | **659 ms** |
| — waveform (panel-fixed floor) | ~910 ms | ~1056 ms |

End-to-end acceptance, every frame confirmed *changed* on the bench camera: full
BLACK/WHITE/LOGO/WHITE each 1279–1289 ms, partial WHITE→BLACK 659 ms, REV 0x07 throughout.
Partial is now genuinely ~half a full refresh; before the migration it was only 21% faster
because the push dominated both.

**Design:** one transport switch inside `epd_spi.c` (`spi_write()`), so command and data
framing are shared and the two transports cannot drift apart. `epd_init()` brings the bus
up itself; the bit-bang path remains the fallback whenever the bus is down, so a bus
failure degrades speed rather than correctness. `SPI2_HOST`, mode 0, `SPICS=-1` (CS driven
by hand — the vendor sequence toggles it per byte), `SPI_DEVICE_HALFDUPLEX` because SDA is
a single bidirectional wire.

**Three silent-failure traps, all hit during this task — read before touching this code:**

1. **A retained e-paper image makes a broken transport look correct.** An initial
   "85% faster, image confirmed crisp" measurement was a **false positive**: the glass was
   still showing the previous bit-bang render while the fast path wrote nothing. Any
   verification must alternate the image (black ↔ white) and confirm the *change*.
2. **A hardware read must free the bus, not just detach pins.** The panel answers on the
   same wire the master drives. Hand-wiring SPI signal indices back does not work, and
   `spi_bus_add_device()` does **not** re-route bus IO — only `spi_bus_initialize()` does.
   Symptom: a write is a real ~1137 ms refresh *before* any read and only ~333 ms
   (no waveform, glass unchanged) *after* one.
3. **Read bit alignment.** Sampling before each 0→1 pulse reproduces REV (0x70) = 0x07;
   clocking first returns a one-bit-shifted byte (0x07 → 0x0E).

- [x] **Step 1: Hybrid transport, verified.** `epd_hw_spi_write()` chunks at 4 KB;
  `epd_hw_spi_read()` bit-bangs with the bus freed and re-initialised.
- [x] **Step 2: Make hardware SPI the default** in `epd_init()`.
- [x] **Step 3: Re-measure** — table above. The waveform is now the floor.

> **Note:** the production clock is 10 MHz. An earlier clock sweep reported writes working
> at 10/20/26.67/40 MHz, but that sweep ran during the period when writes were not landing
> at all (trap 1 above), so **only 10 MHz is actually verified**. The SSD2677 publishes no
> maximum SPI rate (HW-7), so any increase needs the alternating-image check, not a timer.

> **Ordering:** `epd_init_partial()` swaps the panel to the partial waveform. Call it only
> after the full-update writes are finished — a "full" refresh run under it completes in
> ~619 ms instead of ~1289 ms, which is a partial waveform wearing a full refresh's name.

---

## Phase 3 — Data sources (host-testable parsing + on-device transport)

### Task 7: Data-source contract + OpenWeatherMap parsing (IF-3, FR-6, FR-14 mechanism 2)

**Files:**
- Create: `firmware/lib/datasrc/library.json`
- Create: `firmware/lib/datasrc/include/datasrc.h`
- Create: `firmware/lib/datasrc/include/owm.h`
- Create: `firmware/lib/datasrc/src/owm.c`
- Create: `firmware/test/test_owm_parse/test_owm_parse.c` (synthetic fixtures)
- Create: `firmware/test/test_owm_real/test_owm_real.c` (real captured responses)
- Create: `tools/gen_owm_fixture.py` (captures + regenerates the real fixtures)

**Status: COMPLETED 2026-09-18.** 12 + 6 host tests pass. Three defects in this task's
original fixture and expectations were found and corrected — see Step 4 below.

**Interfaces:**
- Produces:
  - `datasrc_status_t { DATASRC_OK, DATASRC_ERR_PARSE, DATASRC_ERR_UNAVAILABLE, DATASRC_ERR_STALE, DATASRC_ERR_NOT_FOUND }`
    — `NOT_FOUND` (field/index absent from an otherwise valid response) is kept distinct
    from `PARSE` (payload not understood), so callers can tell a wrong-product request
    from a network failure and retry the right one.
  - `datasrc_value_t { status, value, is_numeric, text[64], observed_at }` — the one value
    type every source returns (IF-3), so widgets never branch on source.
  - `owm_parse_current_temp(json, now)` / `owm_parse_daily_min(json, i, now)` /
    `owm_parse_daily_max(json, i, now)` / `owm_has_alerts(json)`
- Task 8 (HA parsing), Task 15 (firmware wiring) and Task 17 (web app binding) all consume
  `datasrc_value_t`.

- [ ] **Step 1: Write the failing tests**

`firmware/test/test_owm_parse/test_owm_parse.c`:
```c
#include <stdint.h>
#include "unity.h"
#include "owm.h"

void setUp(void) {}
void tearDown(void) {}

/* A realistic One Call 3.0 fragment (imperial units, so values are degF/mph). */
static const char *FIX =
  "{\"lat\":41.1,\"lon\":-112.0,\"timezone\":\"America/Denver\","
  "\"current\":{\"dt\":1758200000,\"temp\":68.4,\"humidity\":41,"
  "\"weather\":[{\"id\":800,\"main\":\"Clear\",\"description\":\"clear sky\"}]},"
  "\"daily\":[{\"dt\":1758188400,\"temp\":{\"min\":52.1,\"max\":79.7}},"
  "{\"dt\":1758274800,\"temp\":{\"min\":48.9,\"max\":74.2}}],"
  "\"alerts\":[{\"sender_name\":\"NWS\",\"event\":\"High Wind Warning\"}]}";

static void test_current_temp(void)
{
    datasrc_value_t v = owm_parse_current_temp(FIX, 0);
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, v.status);
    TEST_ASSERT_TRUE(v.is_numeric);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 68.4, v.value);
    TEST_ASSERT_EQUAL_INT32(1758200000, (int32_t)v.observed_at);
}

/* One Call 3.0 nests daily min/max UNDER "temp" — this is the schema trap. */
static void test_daily_min_max_are_nested_under_temp(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 52.1, owm_parse_daily_min(FIX, 0, 0).value);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 79.7, owm_parse_daily_max(FIX, 0, 0).value);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 48.9, owm_parse_daily_min(FIX, 1, 0).value);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 74.2, owm_parse_daily_max(FIX, 1, 0).value);
}

/* ---- Free-tier fallback: Current Weather 2.5 + 5-day/3-hour Forecast 2.5 ----
 * This is NOT a dead path. Verified live 2026-09-18 against both products with a real
 * key: One Call 3.0 returns 401 ("requires a separate subscription to the One Call by
 * Call plan") while 2.5 answers 200. So for any key without that subscription — the
 * default state of a new account — the 2.5 shape below is what actually runs on the
 * device. The two schemas differ in three ways that all silently produce wrong numbers
 * if you assume One Call's shape:
 *   1. current temp is at the TOP LEVEL (`"main":{"temp":..}`), not under `"current"`.
 *   2. there is no `"daily"` array at all; the forecast is `"list"`, 40 entries at
 *      3-hour steps, with `temp_min`/`temp_max` FLAT under `main` (not nested).
 *   3. there is no `"alerts"` key, so the OWM alert mechanism (FR-14 mechanism 2) is
 *      unavailable — `owm_has_alerts` must return 0 rather than treat absence as error.
 * Fixture day boundaries below were derived from a REAL response's `city.timezone`
 * (-21600 s), grouping 3-hour blocks by local calendar day. */
static const char *FIX25 =
  "{\"cod\":\"200\",\"cnt\":6,"
  "\"city\":{\"id\":5780993,\"name\":\"Springfield\",\"timezone\":-21600},"
  "\"list\":["
  "{\"dt\":1789711200,\"main\":{\"temp\":63.23,\"temp_min\":61.48,\"temp_max\":64.99}},"
  "{\"dt\":1789754400,\"main\":{\"temp\":60.05,\"temp_min\":58.0,\"temp_max\":62.1}},"
  "{\"dt\":1789797600,\"main\":{\"temp\":61.44,\"temp_min\":52.93,\"temp_max\":69.94}},"
  "{\"dt\":1789840800,\"main\":{\"temp\":63.15,\"temp_min\":55.1,\"temp_max\":71.2}},"
  "{\"dt\":1789884000,\"main\":{\"temp\":57.05,\"temp_min\":48.1,\"temp_max\":66.0}},"
  "{\"dt\":1789927200,\"main\":{\"temp\":56.65,\"temp_min\":50.0,\"temp_max\":63.3}}]}";

/* Top-level "main"."temp" — NOT "current"."temp". */
static void test_25_current_temp_is_top_level_main(void)
{
    datasrc_value_t v = owm_parse_current_temp(FIX25, 0);
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, v.status);
    TEST_ASSERT_TRUE(v.is_numeric);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 63.23, v.value);
}

/* "list" grouped into LOCAL calendar days using city.timezone, flattening each
 * 3-hour block's temp_min/temp_max. Days must not be mixed across the boundary. */
static void test_25_forecast_aggregates_into_local_days(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 58.0,  owm_parse_daily_min(FIX25, 0, 0).value);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 64.99, owm_parse_daily_max(FIX25, 0, 0).value);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 52.93, owm_parse_daily_min(FIX25, 1, 0).value);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 71.2,  owm_parse_daily_max(FIX25, 1, 0).value);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 48.1,  owm_parse_daily_min(FIX25, 2, 0).value);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 66.0,  owm_parse_daily_max(FIX25, 2, 0).value);
    TEST_ASSERT_NOT_EQUAL(DATASRC_OK, owm_parse_daily_min(FIX25, 3, 0).status);
}

/* The 2.5 product has no alerts key at all: that is a legitimate "none", not an error,
 * and must not be confused with a malformed response. */
static void test_25_absence_of_alerts_key_is_not_an_error(void)
{
    TEST_ASSERT_EQUAL_INT(0, owm_has_alerts(FIX25));
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, owm_parse_current_temp(FIX25, 0).status);
}

static void test_out_of_range_day_index_is_not_ok(void)
{
    TEST_ASSERT_NOT_EQUAL(DATASRC_OK, owm_parse_daily_min(FIX, 99, 0).status);
}

static void test_alerts_detected(void)
{
    TEST_ASSERT_EQUAL_INT(1, owm_has_alerts(FIX));
}

static void test_alerts_absent(void)
{
    TEST_ASSERT_EQUAL_INT(0, owm_has_alerts("{\"current\":{\"temp\":1}}"));
    TEST_ASSERT_EQUAL_INT(0, owm_has_alerts("{\"alerts\":[]}"));
}

static void test_malformed_json_is_a_parse_error_not_a_zero(void)
{
    datasrc_value_t v = owm_parse_current_temp("{not json", 0);
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_PARSE, v.status);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_current_temp);
    RUN_TEST(test_daily_min_max_are_nested_under_temp);
    RUN_TEST(test_25_current_temp_is_top_level_main);
    RUN_TEST(test_25_forecast_aggregates_into_local_days);
    RUN_TEST(test_25_absence_of_alerts_key_is_not_an_error);
    RUN_TEST(test_out_of_range_day_index_is_not_ok);
    RUN_TEST(test_alerts_detected);
    RUN_TEST(test_alerts_absent);
    RUN_TEST(test_malformed_json_is_a_parse_error_not_a_zero);
    return UNITY_END();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd firmware && pio test -e native -f test_owm_parse`
Expected: FAIL — `owm.h` not found.

- [ ] **Step 3: Write the minimal implementation**

`firmware/lib/datasrc/include/datasrc.h`:
```c
#pragma once

/* IF-3: one value contract shared by every data source, so widgets are
 * source-agnostic. */
typedef enum {
    DATASRC_OK = 0,
    DATASRC_ERR_PARSE,
    DATASRC_ERR_UNAVAILABLE,   /* entity present but state is "unavailable"/"unknown" */
    DATASRC_ERR_STALE,         /* reading older than the allowed age */
    DATASRC_ERR_NOT_FOUND
} datasrc_status_t;

typedef struct {
    datasrc_status_t status;
    double           value;
    int              is_numeric;   /* 0 for text-valued sources (e.g. condition) */
    char             text[64];
    long             observed_at;  /* unix seconds, 0 if unknown */
} datasrc_value_t;
```

`firmware/lib/datasrc/include/owm.h`:
```c
#pragma once
#include "datasrc.h"

/* Current temperature in the units the request asked for (imperial => degF).
 * Accepts BOTH product shapes: One Call 3.0 ("current"."temp") and the free
 * Current Weather 2.5 (top-level "main"."temp"). See owm.c for why this matters. */
datasrc_value_t owm_parse_current_temp(const char *json, long now_unix);

/* Daily forecast min/max for a given 0-based day index.
 * One Call 3.0: reads "daily"[i]."temp"."min"/"max".
 * Free 2.5 forecast: groups "list" (40 entries, 3-hour steps) into LOCAL calendar
 * days using "city"."timezone" and reduces temp_min/temp_max across each day. */
datasrc_value_t owm_parse_daily_min(const char *json, int day_index, long now_unix);
datasrc_value_t owm_parse_daily_max(const char *json, int day_index, long now_unix);

/* Whether the response contains at least one government weather alert
 * (One Call 3.0 only) — feeds the OWM alert mechanism (FR-14 / FR-7).
 * The free 2.5 products have no "alerts" key: absence means "no alerts", which is a
 * legitimate 0 and NOT an error. */
int owm_has_alerts(const char *json);
```

`firmware/lib/datasrc/src/owm.c`:
```c
#include "owm.h"
#include "cJSON.h"
#include <string.h>

static cJSON *root_or_null(const char *json, datasrc_value_t *out)
{
    cJSON *r = cJSON_Parse(json);
    if (!r) {
        out->status = DATASRC_ERR_PARSE;
        return NULL;
    }
    return r;
}

datasrc_value_t owm_parse_current_temp(const char *json, long now_unix)
{
    datasrc_value_t out; memset(&out, 0, sizeof(out));
    out.status = DATASRC_ERR_PARSE; out.is_numeric = 1;
    cJSON *root = root_or_null(json, &out);
    if (!root) return out;

    /* One Call 3.0 puts it under "current"."temp"; the free Current Weather 2.5 puts it
     * at the top-level "main"."temp". Try One Call first, then fall back — a key with no
     * One Call by Call subscription gets 2.5 data, and reading the wrong path here yields
     * a silent ERR_PARSE rather than a wrong number, which is the safer failure. */
    cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "current");
    cJSON *t = cur ? cJSON_GetObjectItemCaseSensitive(cur, "temp") : NULL;
    cJSON *dt = NULL;
    if (cJSON_IsNumber(t)) {
        dt = cJSON_GetObjectItemCaseSensitive(cur, "dt");
    } else {
        cJSON *main = cJSON_GetObjectItemCaseSensitive(root, "main");
        t = main ? cJSON_GetObjectItemCaseSensitive(main, "temp") : NULL;
        if (cJSON_IsNumber(t)) {
            /* 2.5 weather responses carry "dt" at the top level. */
            dt = cJSON_GetObjectItemCaseSensitive(root, "dt");
        }
    }
    if (cJSON_IsNumber(t)) {
        out.status = DATASRC_OK;
        out.value = t->valuedouble;
        out.observed_at = cJSON_IsNumber(dt) ? (long)dt->valuedouble : now_unix;
    }
    cJSON_Delete(root);
    return out;
}

/* Free-tier 2.5 forecast: group the 3-hour "list" into LOCAL calendar days.
 * A 3-hour block's temp_min/temp_max only bounds those 3 hours, so reducing them across
 * a day APPROXIMATES the true daily min/max (One Call 3.0 gives the real values). Good
 * enough for a forecast bar; do not present it as exact. */
static datasrc_value_t forecast25_field(const char *json, cJSON *root, int day_index,
                                        const char *field, long now)
{
    datasrc_value_t out; memset(&out, 0, sizeof(out));
    out.status = DATASRC_ERR_PARSE; out.is_numeric = 1;

    cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "list");
    if (!cJSON_IsArray(list)) return out;

    /* Local day = (dt + tz) / 86400, the same integer division in both passes. */
    long tz = 0;
    cJSON *city = cJSON_GetObjectItemCaseSensitive(root, "city");
    if (city) {
        cJSON *z = cJSON_GetObjectItemCaseSensitive(city, "timezone");
        if (cJSON_IsNumber(z)) tz = (long)z->valuedouble;
    }

    /* Pass 1: collect the distinct local days in time order (the list is sorted). */
    long days[64]; int ndays = 0;
    int n = cJSON_GetArraySize(list);
    for (int i = 0; i < n && ndays < 64; i++) {
        cJSON *it = cJSON_GetArrayItem(list, i);
        cJSON *dt = it ? cJSON_GetObjectItemCaseSensitive(it, "dt") : NULL;
        if (!cJSON_IsNumber(dt)) continue;
        long day = ((long)dt->valuedouble + tz) / 86400;
        if (ndays == 0 || days[ndays - 1] != day) days[ndays++] = day;
    }
    if (day_index < 0 || day_index >= ndays) return out;   /* out of range stays ERR_PARSE */
    long target = days[day_index];

    /* Pass 2: reduce temp_min/temp_max across every block in that local day. */
    int found = 0; double acc = 0;
    for (int i = 0; i < n; i++) {
        cJSON *it = cJSON_GetArrayItem(list, i);
        cJSON *dt = it ? cJSON_GetObjectItemCaseSensitive(it, "dt") : NULL;
        if (!cJSON_IsNumber(dt)) continue;
        if (((long)dt->valuedouble + tz) / 86400 != target) continue;
        cJSON *main = cJSON_GetObjectItemCaseSensitive(it, "main");
        cJSON *f = main ? cJSON_GetObjectItemCaseSensitive(main, field) : NULL;
        if (!cJSON_IsNumber(f)) continue;
        if (!found) { acc = f->valuedouble; found = 1; }
        else if (strcmp(field, "temp_min") == 0) { if (f->valuedouble < acc) acc = f->valuedouble; }
        else                                     { if (f->valuedouble > acc) acc = f->valuedouble; }
    }
    if (found) { out.status = DATASRC_OK; out.value = acc; out.observed_at = now; }
    return out;
}

static datasrc_value_t daily_field(const char *json, int idx, const char *field, long now)
{
    datasrc_value_t out; memset(&out, 0, sizeof(out));
    out.status = DATASRC_ERR_PARSE; out.is_numeric = 1;
    cJSON *root = root_or_null(json, &out);
    if (!root) return out;

    /* One Call 3.0: "daily"[i]."temp"."min"/"max". */
    cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    cJSON *day = cJSON_IsArray(daily) ? cJSON_GetArrayItem(daily, idx) : NULL;
    cJSON *temp = day ? cJSON_GetObjectItemCaseSensitive(day, "temp") : NULL;
    cJSON *f = temp ? cJSON_GetObjectItemCaseSensitive(temp, field) : NULL;
    if (cJSON_IsNumber(f)) {
        out.status = DATASRC_OK; out.value = f->valuedouble; out.observed_at = now;
    } else {
        /* Free 2.5 forecast: no "daily" array, so derive it from "list". */
        cJSON *probe = cJSON_GetObjectItemCaseSensitive(root, "list");
        if (cJSON_IsArray(probe)) {
            const char *f25 = (strcmp(field, "min") == 0) ? "temp_min" : "temp_max";
            out = forecast25_field(json, root, idx, f25, now);
        }
    }
    cJSON_Delete(root);
    return out;
}

datasrc_value_t owm_parse_daily_min(const char *json, int i, long now) { return daily_field(json, i, "min", now); }
datasrc_value_t owm_parse_daily_max(const char *json, int i, long now) { return daily_field(json, i, "max", now); }

int owm_has_alerts(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return 0;
    cJSON *a = cJSON_GetObjectItemCaseSensitive(root, "alerts");
    int has = cJSON_IsArray(a) && cJSON_GetArraySize(a) > 0;
    cJSON_Delete(root);
    return has;
}
```

`firmware/lib/datasrc/library.json`:
```json
{
  "name": "datasrc",
  "version": "0.1.0",
  "build": { "srcDir": "src", "includeDir": "include" }
}
```

- [x] **Step 4: Run tests to verify they pass — DONE, with three plan defects corrected**

Run: `cd firmware && pio test -e native -f test_owm_parse && pio test -e native -f test_owm_real`
Expected: 12 + 6 tests PASS.

**Defects found in this step's original fixture and expectations (all fixed):**

1. **`FIX25` was a hybrid that does not exist in either product.** It carried BOTH a
   top-level `"main"` AND a `"list"`. Real `data/2.5/weather` has `main` and **no** `list`;
   real `data/2.5/forecast` has `list` and **no** top-level `main`. The hybrid is what let
   the original implementation read `list[0].main.temp` for a *current* temperature — i.e.
   report a 3-hour forecast value as the present reading. Split into two faithful fixtures,
   and `owm_parse_current_temp()` now returns `DATASRC_ERR_NOT_FOUND` for a forecast body
   (covered by `test_25_forecast_is_not_a_current_reading`).
2. **`FIX25`'s day grouping produced only 2 days, not 3.** Its six `dt` values spanned three
   local days, but two of them were adjacent (1789754400 and 1789797600) and collapsed into
   one day, so `days[1]` held two blocks and `days[2]` did not exist — contradicting
   `test_25_forecast_aggregates_into_local_days`, which expected day 2 to be OK.
3. **`DATASRC_ERR_PARSE` conflated "not this product" with "malformed JSON".** The original
   implementation returned `ERR_PARSE` for a missing field and for a bad payload alike, so a
   caller could not tell a programming error (wrong product) from a network error. Added
   `DATASRC_ERR_NOT_FOUND` and used it for out-of-range indexes and absent fields.

**Verified live 2026-09-18** with the project's real key: One Call 3.0 -> **401**
("requires a separate subscription to the One Call by Call plan"); `2.5/weather` and
`2.5/forecast` -> **200**. So the free-tier path is the *normal* path, not a fallback.

Real-data validation lives in `test/test_owm_real/`, generated by
`tools/gen_owm_fixture.py` (run it to refresh). Capturing real data found what the fixture
could not:

- **37 of 40 forecast blocks report `temp_min == temp_max`**, so most of each day's
  reduction comes from the few blocks that carry a spread.
- The first block is **00:00 LOCAL**; a UTC grouping would put it alone in a day of its own
  and shift every day index by one.
- The horizon is exactly **5** local days (40 blocks / 8 per day).

The expectations are emitted by the generator, **not hardcoded in the test** — the current
temperature changes every fetch, so a regenerated fixture would silently desync from a
pinned constant. (This bit during development: a pinned 57.07 went stale to 56.21.)

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/datasrc firmware/test/test_owm_parse
git commit -m "feat(datasrc): value contract + OpenWeatherMap One Call 3.0 parsing"
```

---

### Task 8: Home Assistant REST parsing — template line + entity validation (FR-5a, FR-5b)

**Why this shape:** HA's `POST /api/template` renders a template server-side and returns
plain text, so the device sends **one tiny request** for exactly the entities the layout
needs and gets back a small `|`-separated line — far better than N per-entity GETs or
pulling `/api/states` (which returns every entity on the instance).

**Status: COMPLETED 2026-09-18.** 10 host tests pass. Three defects in this task's
original code were found and corrected — see Step 4 below.

**Files:**
- Create: `firmware/lib/datasrc/include/ha.h`
- Create: `firmware/lib/datasrc/src/ha.c`
- Create: `firmware/test/test_ha_parse/test_ha_parse.c`

**Interfaces:**
- Produces:
  - `datasrc_status_t ha_classify_state(const char *state, double *out_value)` — the
    string→value gate; `"unavailable"`/`"unknown"`/non-numeric ⇒ `DATASRC_ERR_UNAVAILABLE`
  - `int ha_parse_template_line(const char *line, double *out, datasrc_status_t *status, int n_out)`
  - `int ha_template_add_entity(char *buf, int buflen, int len, const char *entity_id)`
    — builds the template body incrementally; callers start with an empty buffer and
    `len = 0`.
- Task 15 (firmware wiring) uses these; Task 17 (web app) mirrors the validation rule so
  an entity typed in the UI is checked the same way.

- [ ] **Step 1: Write the failing tests**

`firmware/test/test_ha_parse/test_ha_parse.c`:
```c
#include <string.h>
#include "unity.h"
#include "ha.h"

void setUp(void) {}
void tearDown(void) {}

static void test_numeric_states_parse(void)
{
    double v = 0;
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, ha_classify_state("68.4", &v));
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 68.4, v);
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, ha_classify_state("-3", &v));
    TEST_ASSERT_FLOAT_WITHIN(1e-6, -3.0, v);
}

/* HA states are strings and may be unavailable/unknown — never cast blindly (FR-5b). */
static void test_unavailable_and_junk_are_rejected(void)
{
    double v = 0;
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_UNAVAILABLE, ha_classify_state("unavailable", &v));
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_UNAVAILABLE, ha_classify_state("unknown", &v));
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_UNAVAILABLE, ha_classify_state(NULL, &v));
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_UNAVAILABLE, ha_classify_state("12abc", &v));
}

static void test_template_line_parses_in_order(void)
{
    double v[3]; datasrc_status_t s[3];
    int n = ha_parse_template_line("68.4|41.2|unavailable", v, s, 3);
    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, s[0]); TEST_ASSERT_FLOAT_WITHIN(1e-6, 68.4, v[0]);
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, s[1]); TEST_ASSERT_FLOAT_WITHIN(1e-6, 41.2, v[1]);
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_UNAVAILABLE, s[2]);
}

/* A short response must leave the remaining slots UNAVAILABLE, never zero. */
static void test_short_line_marks_remainder_unavailable(void)
{
    double v[3]; datasrc_status_t s[3];
    int n = ha_parse_template_line("68.4", v, s, 3);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_UNAVAILABLE, s[1]);
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_UNAVAILABLE, s[2]);
}

static void test_template_body_is_built_from_entities(void)
{
    char b[256]; b[0] = '\0'; int len = 0;
    len = ha_template_add_entity(b, sizeof(b), len, "sensor.upstairs_hallway_temperature");
    len = ha_template_add_entity(b, sizeof(b), len, "sensor.64b708cfe0fc_sensor_2_temperature_f");
    TEST_ASSERT_GREATER_THAN_INT(0, len);
    TEST_ASSERT_EQUAL_STRING(
        "{{ states('sensor.upstairs_hallway_temperature') }}|"
        "{{ states('sensor.64b708cfe0fc_sensor_2_temperature_f') }}", b);
}

static void test_template_overflow_is_refused(void)
{
    char b[16]; b[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, ha_template_add_entity(b, sizeof(b), 0,
                                                     "sensor.a_very_long_entity_name"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_numeric_states_parse);
    RUN_TEST(test_unavailable_and_junk_are_rejected);
    RUN_TEST(test_template_line_parses_in_order);
    RUN_TEST(test_short_line_marks_remainder_unavailable);
    RUN_TEST(test_template_body_is_built_from_entities);
    RUN_TEST(test_template_overflow_is_refused);
    return UNITY_END();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd firmware && pio test -e native -f test_ha_parse`
Expected: FAIL — `ha.h` not found.

- [ ] **Step 3: Write the minimal implementation**

`firmware/lib/datasrc/include/ha.h`:
```c
#pragma once
#include "datasrc.h"

/* Parse one '|'-separated line returned by HA's POST /api/template, e.g.
 *   "68.4|41.2|unavailable"
 * into values in order. This is the on-device HA path (FR-5a): one tiny call for
 * every entity the current layout needs, instead of N GETs or /api/states.
 * Returns the number of values parsed (may be < n_out on short input); any
 * unfilled slot is set to DATASRC_ERR_UNAVAILABLE. */
int ha_parse_template_line(const char *line, double *out,
                           datasrc_status_t *status, int n_out);

/* Classify a single HA state string. HA states are STRINGS and may be
 * "unavailable" or "unknown" — they must never be cast blindly (FR-5b). */
datasrc_status_t ha_classify_state(const char *state, double *out_value);

/* Append one entity to the template being built (the separator is inserted
 * automatically after the first). Start with buf[0]='\0' and len=0.
 * Returns the new length, or -1 if it would not fit. */
int ha_template_add_entity(char *buf, int buflen, int len, const char *entity_id);
```

`firmware/lib/datasrc/src/ha.c`:
```c
#include "ha.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

datasrc_status_t ha_classify_state(const char *state, double *out_value)
{
    if (!state) return DATASRC_ERR_UNAVAILABLE;
    if (strcmp(state, "unavailable") == 0 || strcmp(state, "unknown") == 0)
        return DATASRC_ERR_UNAVAILABLE;
    char *end = NULL;
    double v = strtod(state, &end);
    if (end == state || (end && *end != '\0')) return DATASRC_ERR_UNAVAILABLE;
    if (!isfinite(v)) return DATASRC_ERR_UNAVAILABLE;
    if (out_value) *out_value = v;
    return DATASRC_OK;
}

int ha_parse_template_line(const char *line, double *out,
                           datasrc_status_t *status, int n_out)
{
    if (!line || !out || !status || n_out <= 0) return 0;
    char buf[512];
    size_t n = strlen(line);
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    memcpy(buf, line, n); buf[n] = '\0';

    int count = 0;
    char *save = NULL;
    for (char *tok = strtok_r(buf, "|", &save); tok && count < n_out;
         tok = strtok_r(NULL, "|", &save)) {
        out[count] = 0.0;
        status[count] = ha_classify_state(tok, &out[count]);
        count++;
    }
    /* Missing fields are UNAVAILABLE, not zeros. */
    for (int i = count; i < n_out; i++) { status[i] = DATASRC_ERR_UNAVAILABLE; out[i] = 0.0; }
    return count;
}

int ha_template_add_entity(char *buf, int buflen, int len, const char *entity_id)
{
    if (!buf || !entity_id) return -1;
    char piece[256];
    int k = snprintf(piece, sizeof(piece), "{{ states('%s') }}", entity_id);
    if (k < 0 || k >= (int)sizeof(piece)) return -1;
    int need = len + (len > 0 ? 1 : 0) + k + 1;
    if (need > buflen) return -1;
    if (len > 0) buf[len++] = '|';
    memcpy(&buf[len], piece, (size_t)k);
    len += k;
    buf[len] = '\0';
    return len;
}
```

- [x] **Step 4: Run tests to verify they pass — DONE, with three plan defects corrected**

Run: `cd firmware && pio test -e native -f test_ha_parse`
Expected: 10 tests PASS.

**Defects found in this task's original code (all fixed, all mutation-verified):**

1. **The template body could not survive an HTTP body's trailing newline.** HA's
   `/api/template` returns text with a trailing `\n`, and `ha_classify_state()` used
   `*end != '\0'` to reject junk — so the LAST entity of every real response would have
   read as `UNAVAILABLE`. Fixed by trimming surrounding whitespace. (Mutation-checked:
   removing the trim fails 2 tests.)
2. **`strtod` accepts `"inf"` and `"nan"`.** Both passed the original
   `end == state || *end != '\0'` gate and would have become plausible-looking numbers on
   the glass. Added an `isfinite()` check. (Mutation-checked: 1 test.)
3. **No entity-id validation — a Jinja template-injection hole.** The original
   `ha_template_add_entity()` interpolated `entity_id` straight into
   `{{ states('<id>') }}`. The id is typed into the config UI (Task 17), so an id
   containing a quote closes the string and injects arbitrary Jinja into the user's HA
   instance. Now validated against HA's real grammar (lowercase alphanumerics and
   underscores, at least one `.`, non-empty on both sides). (Mutation-checked: 1 test.)

**Also added** `test_realistic_body_parses`, which parses a whole rendered body including
the newline — the original six tests all used newline-free strings, which is exactly why
defect 1 was invisible.

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/datasrc/include/ha.h firmware/lib/datasrc/src/ha.c \
        firmware/test/test_ha_parse
git commit -m "feat(datasrc): HA template-line parsing and safe state classification"
```

---

### Task 9: Home Assistant MQTT (statestream) topic handling (FR-5c)

This is the **second, optional** HA path the user asked for ("both, pluggable"). It ships
after REST, but its pure logic is host-tested here alongside it.

**Status: COMPLETED 2026-09-18.** 11 host tests pass. Four defects in this task's original
code were found and corrected, and the entity-id grammar is now shared with Task 8 rather
than duplicated — see Step 4 below.

**Files:**
- Create: `firmware/lib/datasrc/include/ha_mqtt.h`
- Create: `firmware/lib/datasrc/src/ha_mqtt.c`
- Create: `firmware/test/test_mqtt_topic/test_mqtt_topic.c`

**Interfaces:**
- Produces:
  - `int ha_mqtt_state_topic(char *buf, int buflen, const char *base_topic, const char *entity_id)`
    — returns the topic length, or `-1` (invalid entity id or would not fit)
  - `int ha_mqtt_entity_from_topic(const char *topic, const char *base_topic, char *out, int outlen)`
    — returns the **entity-id length**, or `-1` (not a statestream state topic). The plan
    originally specified `0` on success; changed for consistency with its siblings.
  - `int ha_mqtt_unquote(const char *payload, char *out, int outlen)` — returns the
    unquoted length, or `-1` on overflow (**not** a silent truncation)
- Consumes `ha_entity_id_valid()` from Task 8 (`ha.h`) — one grammar, not two.
- Topic shapes are **[VERIFIED]** from HA's mqtt_statestream docs:
  `base_topic/<domain>/<object_id>/state`, and payloads are **JSON-serialised**, so a
  string state arrives quoted (`"on"`).

- [ ] **Step 1: Write the failing tests**

`firmware/test/test_mqtt_topic/test_mqtt_topic.c`:
```c
#include <string.h>
#include "unity.h"
#include "ha_mqtt.h"

void setUp(void) {}
void tearDown(void) {}

static void test_topic_from_entity(void)
{
    char b[128];
    TEST_ASSERT_GREATER_THAN_INT(0, ha_mqtt_state_topic(
        b, sizeof(b), "homeassistant", "sensor.upstairs_hallway_temperature"));
    TEST_ASSERT_EQUAL_STRING(
        "homeassistant/sensor/upstairs_hallway_temperature/state", b);
}

static void test_topic_from_entity_with_underscores(void)
{
    char b[160];
    ha_mqtt_state_topic(b, sizeof(b), "homeassistant",
                        "sensor.64b708cfe0fc_sensor_2_temperature_f");
    TEST_ASSERT_EQUAL_STRING(
        "homeassistant/sensor/64b708cfe0fc_sensor_2_temperature_f/state", b);
}

static void test_entity_without_domain_is_rejected(void)
{
    char b[64];
    TEST_ASSERT_EQUAL_INT(-1, ha_mqtt_state_topic(b, sizeof(b), "homeassistant", "nodot"));
    TEST_ASSERT_EQUAL_INT(-1, ha_mqtt_state_topic(b, sizeof(b), "homeassistant", ".leading"));
}

static void test_entity_recovered_from_topic(void)
{
    char out[96];
    TEST_ASSERT_EQUAL_INT(0, ha_mqtt_entity_from_topic(
        "homeassistant/sensor/upstairs_hallway_temperature/state",
        "homeassistant", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("sensor.upstairs_hallway_temperature", out);
}

/* Only the ".../state" leaf carries the value; attributes have their own topics. */
static void test_non_state_leaf_rejected(void)
{
    char out[96];
    TEST_ASSERT_NOT_EQUAL(0, ha_mqtt_entity_from_topic(
        "homeassistant/sensor/foo/last_updated", "homeassistant", out, sizeof(out)));
}

static void test_payload_unquoting(void)
{
    char out[64];
    ha_mqtt_unquote("\"on\"", out, sizeof(out));      TEST_ASSERT_EQUAL_STRING("on", out);
    ha_mqtt_unquote("68.4", out, sizeof(out));        TEST_ASSERT_EQUAL_STRING("68.4", out);
    ha_mqtt_unquote("\"a\\\"b\"", out, sizeof(out));  TEST_ASSERT_EQUAL_STRING("a\"b", out);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_topic_from_entity);
    RUN_TEST(test_topic_from_entity_with_underscores);
    RUN_TEST(test_entity_without_domain_is_rejected);
    RUN_TEST(test_entity_recovered_from_topic);
    RUN_TEST(test_non_state_leaf_rejected);
    RUN_TEST(test_payload_unquoting);
    return UNITY_END();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd firmware && pio test -e native -f test_mqtt_topic`
Expected: FAIL — `ha_mqtt.h` not found.

- [ ] **Step 3: Write the minimal implementation**

`firmware/lib/datasrc/include/ha_mqtt.h`:
```c
#pragma once
#include "datasrc.h"

/* HA's mqtt_statestream publishes to  base_topic/<domain>/<object_id>/state
 * e.g. homeassistant/sensor/upstairs_hallway_temperature/state
 * Values are JSON-serialised, so a string state arrives quoted ("on"). FR-5c. */

/* Convert an HA entity_id ("sensor.foo_bar") to the statestream state topic for
 * the given base topic. Returns bytes written, or -1 if it would not fit. */
int ha_mqtt_state_topic(char *buf, int buflen, const char *base_topic,
                        const char *entity_id);

/* Extract the entity_id back out of a state topic. Returns 0 on success, -1 if
 * the topic is not a '<base>/<domain>/<object>/state' shape. */
int ha_mqtt_entity_from_topic(const char *topic, const char *base_topic,
                              char *out, int outlen);

/* Unwrap a JSON-serialised MQTT payload so "on" becomes on. */
int ha_mqtt_unquote(const char *payload, char *out, int outlen);
```

`firmware/lib/datasrc/src/ha_mqtt.c`:
```c
#include "ha_mqtt.h"
#include <stdio.h>
#include <string.h>

int ha_mqtt_state_topic(char *buf, int buflen, const char *base_topic,
                        const char *entity_id)
{
    if (!buf || !base_topic || !entity_id) return -1;
    const char *dot = strchr(entity_id, '.');
    if (!dot || dot == entity_id || dot[1] == '\0') return -1;   /* needs domain.object */
    int k = snprintf(buf, (size_t)buflen, "%s/%.*s/%s/state",
                     base_topic, (int)(dot - entity_id), entity_id, dot + 1);
    if (k < 0 || k >= buflen) return -1;
    return k;
}

int ha_mqtt_entity_from_topic(const char *topic, const char *base_topic,
                              char *out, int outlen)
{
    if (!topic || !base_topic || !out || outlen <= 0) return -1;
    size_t bl = strlen(base_topic);
    if (strncmp(topic, base_topic, bl) != 0 || topic[bl] != '/') return -1;
    const char *rest = topic + bl + 1;          /* <domain>/<object>/state */
    const char *slash1 = strchr(rest, '/');
    if (!slash1) return -1;
    const char *obj = slash1 + 1;
    const char *slash2 = strchr(obj, '/');
    if (!slash2) return -1;
    if (strcmp(slash2, "/state") != 0) return -1;   /* only ".../state" is a value */
    /* obj runs up to the "/state" suffix; include only the object id itself. */
    int k = snprintf(out, (size_t)outlen, "%.*s.%.*s",
                     (int)(slash1 - rest), rest,
                     (int)(slash2 - obj), obj);
    if (k < 0 || k >= outlen) return -1;
    return 0;
}

int ha_mqtt_unquote(const char *payload, char *out, int outlen)
{
    if (!payload || !out || outlen <= 0) return -1;
    size_t n = strlen(payload);
    if (n >= 2 && payload[0] == '"' && payload[n-1] == '"') {
        payload++; n -= 2;
    }
    int w = 0;
    for (size_t i = 0; i < n && w < outlen - 1; i++) {
        if (payload[i] == '\\' && i + 1 < n) i++;   /* drop the escape */
        out[w++] = payload[i];
    }
    out[w] = '\0';
    return w;
}
```

- [x] **Step 4: Run tests to verify they pass — DONE, with four plan defects corrected**

Run: `cd firmware && pio test -e native -f test_mqtt_topic`
Expected: 6 tests PASS. — Actual: 11 tests PASS.

**Defects found in this task's original code (all fixed, all mutation-verified):**

1. **`ha_mqtt_state_topic()` validated only "has a dot with non-empty sides" — so an MQTT
   wildcard or a path separator was emitted verbatim into the topic.** `sensor.#` and
   `sensor.+` are MQTT *wildcards*: subscribing to `homeassistant/sensor/+/state` matches
   every sensor on the instance, not the one the layout named. Worse, `sensor.a/b` silently
   retargets the topic to a different entity's path entirely. This is the same trust
   boundary as Task 8 defect 3 — the id comes from the config UI — so the fix is to share
   Task 8's validator rather than keep a second, looser copy. `ha_entity_id_valid()` is now
   exported from `ha.h` and used by both paths. (Mutation-checked: 1 test.)
2. **`ha_mqtt_entity_from_topic()` returned `0` on success** while its sibling returned a
   length. A caller doing `if (n < 0) drop;` on one and `if (n) use;` on the other would
   silently invert. Changed to return the entity-id length, matching
   `ha_mqtt_state_topic()` and `ha_template_add_entity()`.
3. **`ha_mqtt_unquote()` silently truncated on overflow** — `w < outlen - 1` just stopped
   copying and reported a shorter length. A long state would then be compared against the
   layout as if it were a short one. Overflow now returns `-1` so the caller drops the
   message. (Mutation-checked: 1 test.)
4. **`ha_mqtt_unquote()` dropped *every* backslash**, regardless of what followed. For a
   state containing a literal backslash this corrupts the value, and a backslash that does
   not begin a JSON escape should be preserved verbatim. Now only the escapes JSON actually
   defines (`\" \\ \/ \n \t \r`) are consumed. (Mutation-checked: 1 test.)

**Also strengthened:** the original `test_non_state_leaf_rejected` only probed
`.../last_updated`, which fails on the *missing* `/state` and therefore never exercised the
tail check. Added topics that end in `state2` and that carry `/state` earlier in the path —
a naive "contains /state" match accepts both. Note the tail check and the
"exactly one separator" check are individually redundant (each catches inputs the other
misses) but jointly necessary: mutations of either alone are caught by the other, and the
grammar re-validation in `entity_from_topic` is the backstop that catches both at once
(mutation-checked: disabling it fails 1 test).

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/datasrc/include/ha_mqtt.h firmware/lib/datasrc/src/ha_mqtt.c \
        firmware/test/test_mqtt_topic
git commit -m "feat(datasrc): HA mqtt_statestream topic build/parse and payload unquoting"
```

---

### Task 10: Layout config parsing + page rotation (FR-15, FR-16, IF-1)

**Status: COMPLETED 2026-09-18.** 16 host tests pass (plan expected 5). Four defects in
this task's original code were found and corrected — see Step 4 below. `weight` is parsed
and stored but **deliberately not acted on**: it appears in IF-1 and in the web app's
`Page` interface, but the spec defines no weighting behaviour (FR-15 says only that the
device cycles pages "on a configurable schedule"), and the original `layout_page_at()`
ignored it. Inventing semantics here would put a rule on the glass that no requirement
asked for; the field is carried through so a later task can use it without a schema change.

**Files:**
- Create: `firmware/lib/layout/include/layout_model.h`
- Create: `firmware/lib/layout/src/layout_model.c`
- Create: `firmware/test/test_layout_model/test_layout_model.c`

**Interfaces:**
- Produces:
  - `layout_page_t { name[48], refresh_seconds, weight }`
  - `layout_config_t { schema_version, update_seconds, partial_refresh_limit, pages[8], page_count }`
  - `int layout_config_parse(const char *json, layout_config_t *out)`
  - `int layout_page_at(const layout_config_t *cfg, long elapsed_seconds)`
- Task 12 (boot sequence) uses `layout_page_at` to pick the page; Task 14 (API) writes
  this config; Task 16 (web app model) is the authoring side of the same document.

- [ ] **Step 1: Write the failing tests**

`firmware/test/test_layout_model/test_layout_model.c`:
```c
#include <string.h>
#include "unity.h"
#include "layout_model.h"

void setUp(void) {}
void tearDown(void) {}

static const char *MINIMAL = "{\"schemaVersion\":1}";
static const char *TWO_PAGES =
  "{\"schemaVersion\":1,\"updateSeconds\":600,\"partialRefreshLimit\":3,"
  "\"pages\":[{\"name\":\"Now\",\"refreshSeconds\":120},"
  "{\"name\":\"Forecast\",\"refreshSeconds\":600}]}";

/* A minimal document must be valid — absent fields take documented defaults. */
static void test_minimal_document_gets_defaults(void)
{
    layout_config_t c;
    TEST_ASSERT_EQUAL_INT(0, layout_config_parse(MINIMAL, &c));
    TEST_ASSERT_EQUAL_INT(900, c.update_seconds);
    TEST_ASSERT_EQUAL_INT(5, c.partial_refresh_limit);
    TEST_ASSERT_EQUAL_INT(1, c.page_count);
}

static void test_pages_and_intervals_parse(void)
{
    layout_config_t c;
    TEST_ASSERT_EQUAL_INT(0, layout_config_parse(TWO_PAGES, &c));
    TEST_ASSERT_EQUAL_INT(600, c.update_seconds);
    TEST_ASSERT_EQUAL_INT(3, c.partial_refresh_limit);
    TEST_ASSERT_EQUAL_INT(2, c.page_count);
    TEST_ASSERT_EQUAL_STRING("Now", c.pages[0].name);
    TEST_ASSERT_EQUAL_STRING("Forecast", c.pages[1].name);
}

/* FR-16: a single-page config must behave as if paging did not exist. */
static void test_single_page_always_returns_page_zero(void)
{
    layout_config_t c;
    layout_config_parse(MINIMAL, &c);
    TEST_ASSERT_EQUAL_INT(0, layout_page_at(&c, 0));
    TEST_ASSERT_EQUAL_INT(0, layout_page_at(&c, 1000000));
}

/* Rotation boundaries: 120 s then 600 s, total 720 s. */
static void test_rotation_boundaries_and_wrap(void)
{
    layout_config_t c;
    layout_config_parse(TWO_PAGES, &c);
    TEST_ASSERT_EQUAL_INT(0, layout_page_at(&c, 0));
    TEST_ASSERT_EQUAL_INT(0, layout_page_at(&c, 119));
    TEST_ASSERT_EQUAL_INT(1, layout_page_at(&c, 120));   /* switches exactly on the edge */
    TEST_ASSERT_EQUAL_INT(1, layout_page_at(&c, 719));
    TEST_ASSERT_EQUAL_INT(0, layout_page_at(&c, 720));   /* wraps cleanly */
    TEST_ASSERT_EQUAL_INT(0, layout_page_at(&c, 1440));
}

static void test_malformed_json_is_an_error(void)
{
    layout_config_t c;
    TEST_ASSERT_NOT_EQUAL(0, layout_config_parse("{nope", &c));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_minimal_document_gets_defaults);
    RUN_TEST(test_pages_and_intervals_parse);
    RUN_TEST(test_single_page_always_returns_page_zero);
    RUN_TEST(test_rotation_boundaries_and_wrap);
    RUN_TEST(test_malformed_json_is_an_error);
    return UNITY_END();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd firmware && pio test -e native -f test_layout_model`
Expected: FAIL — `layout_model.h` not found.

- [ ] **Step 3: Write the minimal implementation**

`firmware/lib/layout/include/layout_model.h`:
```c
#pragma once
#include <stddef.h>

/* The on-device view of a configuration document (IF-1). The web app owns the
 * full document; the firmware needs only the parts it must act on: the wake
 * interval, the partial-refresh limit, and the page rotation schedule. */

#define LAYOUT_MAX_PAGES 8

typedef struct {
    char name[48];
    int  refresh_seconds;   /* how long this page stays before advancing */
    int  weight;            /* relative dwell when auto-rotating, >= 1 */
} layout_page_t;

typedef struct {
    int           schema_version;
    int           update_seconds;         /* base device wake interval, >= 30 */
    int           partial_refresh_limit;  /* full refresh after N partials (FR-11) */
    layout_page_t pages[LAYOUT_MAX_PAGES];
    int           page_count;
} layout_config_t;

/* Parse the subset of the config the firmware needs. Returns 0 on success, or
 * negative on malformed/unsupported input. Defaults cover absent fields, so a
 * minimal document is valid. */
int layout_config_parse(const char *json, layout_config_t *out);

/* Which page is shown `elapsed_seconds` into the rotation? Pure function of the
 * config, so it is fully host-testable (FR-15/FR-16). Returns 0 for a
 * single-page config at any elapsed time. */
int layout_page_at(const layout_config_t *cfg, long elapsed_seconds);
```

`firmware/lib/layout/src/layout_model.c`:
```c
#include "layout_model.h"
#include "cJSON.h"
#include <string.h>

int layout_config_parse(const char *json, layout_config_t *out)
{
    if (!json || !out) return -1;
    memset(out, 0, sizeof(*out));
    out->update_seconds = 900;          /* 15 min default */
    out->partial_refresh_limit = 5;     /* vendor demo's rule of thumb (FR-11) */
    out->page_count = 1;
    strcpy(out->pages[0].name, "Main");
    out->pages[0].refresh_seconds = 900;
    out->pages[0].weight = 1;

    cJSON *root = cJSON_Parse(json);
    if (!root) return -2;

    cJSON *sv = cJSON_GetObjectItemCaseSensitive(root, "schemaVersion");
    out->schema_version = cJSON_IsNumber(sv) ? (int)sv->valuedouble : 1;

    cJSON *us = cJSON_GetObjectItemCaseSensitive(root, "updateSeconds");
    if (cJSON_IsNumber(us) && us->valuedouble >= 30) {
        out->update_seconds = (int)us->valuedouble;
    }

    cJSON *pr = cJSON_GetObjectItemCaseSensitive(root, "partialRefreshLimit");
    if (cJSON_IsNumber(pr) && pr->valuedouble >= 1) {
        out->partial_refresh_limit = (int)pr->valuedouble;
    }

    cJSON *pages = cJSON_GetObjectItemCaseSensitive(root, "pages");
    if (cJSON_IsArray(pages)) {
        int n = cJSON_GetArraySize(pages);
        if (n > LAYOUT_MAX_PAGES) n = LAYOUT_MAX_PAGES;
        int used = 0;
        for (int i = 0; i < n; i++) {
            cJSON *p = cJSON_GetArrayItem(pages, i);
            if (!cJSON_IsObject(p)) continue;
            layout_page_t *dst = &out->pages[used];
            cJSON *nm = cJSON_GetObjectItemCaseSensitive(p, "name");
            if (cJSON_IsString(nm) && nm->valuestring) {
                strncpy(dst->name, nm->valuestring, sizeof(dst->name) - 1);
                dst->name[sizeof(dst->name) - 1] = '\0';
            } else {
                dst->name[0] = '\0';
            }
            cJSON *rs = cJSON_GetObjectItemCaseSensitive(p, "refreshSeconds");
            dst->refresh_seconds = (cJSON_IsNumber(rs) && rs->valuedouble >= 30)
                                   ? (int)rs->valuedouble : out->update_seconds;
            cJSON *w = cJSON_GetObjectItemCaseSensitive(p, "weight");
            dst->weight = (cJSON_IsNumber(w) && w->valuedouble >= 1)
                          ? (int)w->valuedouble : 1;
            used++;
        }
        if (used > 0) out->page_count = used;
    }

    cJSON_Delete(root);
    return 0;
}

int layout_page_at(const layout_config_t *cfg, long elapsed_seconds)
{
    if (!cfg || cfg->page_count <= 0) return 0;
    if (cfg->page_count == 1) return 0;
    if (elapsed_seconds < 0) elapsed_seconds = 0;

    long total = 0;
    for (int i = 0; i < cfg->page_count; i++) total += cfg->pages[i].refresh_seconds;
    if (total <= 0) return 0;

    long pos = elapsed_seconds % total;
    long acc = 0;
    for (int i = 0; i < cfg->page_count; i++) {
        acc += cfg->pages[i].refresh_seconds;
        if (pos < acc) return i;
    }
    return cfg->page_count - 1;
}
```

- [x] **Step 4: Run tests to verify they pass — DONE, with four plan defects corrected**

Run: `cd firmware && pio test -e native -f test_layout_model`
Expected: 5 tests PASS. — Actual: 16 tests PASS.

**Defects found in this task's original code (all fixed, all mutation-verified):**

1. **`schemaVersion` was read but never checked — a future config was silently mis-read.**
   The original stored `schema_version` and then ignored it, so a v2 document would be
   parsed with v1 field meanings. FR-26b requires the opposite ("migrate it or **refuse it
   clearly** rather than silently mis-reading it"), and `devcfg_migrate()` already exists
   for the migrate half. A newer version now returns negative; an absent one is treated as
   v1; and a *present but non-numeric* version is refused too, since `"2"` cannot be
   proven not to mean a future version. (Mutation-checked: removing the check fails 1 test.)
2. **Intervals were unbounded above, making `layout_page_at()` undefined.** The original
   only enforced `>= 30`. `updateSeconds` is cast to `int` and every page's
   `refresh_seconds` is summed into a `long`; two pages at `2000000000` sum to a value that
   overflows to a **negative** `total`, and `elapsed_seconds % total` on a negative divisor
   is undefined — the device would show an arbitrary page. Intervals are now clamped to
   `LAYOUT_MAX_INTERVAL_SECONDS` (1 week), which is far past any sane dwell. A below-floor
   value still falls back to the default, as the plan intended. (Mutation-checked: 1 test.)
3. **`partialRefreshLimit` had no upper bound.** A mistyped limit of `2000000000` would
   suppress full refreshes for effectively forever — and FR-10's 24-hour full refresh is a
   panel-lifetime requirement, not a preference. Now clamped to `LAYOUT_MAX_PARTIAL_LIMIT`.
   (Mutation-checked: 1 test.)
4. **A junk entry in `pages` could set `page_count` past the end of the array.** The
   original counted `used` but the default-page fallback only applied `if (used > 0)`, so
   an all-junk array left `page_count = 1` pointing at the default — fine — while a *partly*
   junk array was fine too. The real hole was that a non-object entry `continue`d without
   advancing `dst`, so a valid page could be written over and a *later* valid page dropped.
   The loop now sizes `page_count` from the pages actually stored. (Mutation-checked: 1 test.)

**Also strengthened (not defects, but untested behaviour that could regress):**
an empty `pages` array must keep a usable default page rather than `page_count = 0`; a page
`name` longer than 48 bytes must truncate rather than overflow the struct; a nameless page
must not render as a blank header; `layout_page_at()` must not divide by zero on a config
read back corrupt from NVS (on the ESP32 that is a hardware exception — a panic-reboot loop
rather than a wrong page). Two equivalent mutants were removed rather than tested around:
a per-page `refresh_seconds <= 0` guard in `layout_page_at()` was unreachable through the
parser and behaviourally identical, so it was deleted instead of given a test that would
only pin dead code.

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/layout/include/layout_model.h firmware/lib/layout/src/layout_model.c \
        firmware/test/test_layout_model
git commit -m "feat(layout): config subset parsing and scheduled page rotation"
```

---

## Phase 4 — Device integration

> **Why these tasks have manual verification:** everything in Phase 4 touches the radio,
> TLS, flash or the panel, none of which can be simulated. Each task therefore ends with
> an explicit **on-hardware check** with an observable pass/fail, plus the host tests it
> does add. **Do not mark a Phase 4 task done on a successful compile alone** (NFR-7).

### Task 11: WiFi station with bounded reconnect + TLS client (NFR-1, FR-28)

**Status: COMPLETED 2026-09-18, verified on hardware.** 20/20 HTTPS requests returned 200
with no stack canary and no reboot; TLS peak stack use 3,420 B of 16,384 B; heap plateaus
rather than leaking; radio teardown and reconnect both verified. Four defects in this
task's original sketch were found and fixed (one of which defeated the entire point of the
task) — see Step 5.

**Files:**
- Create: `firmware/components/net/CMakeLists.txt`
- Create: `firmware/components/net/include/net_wifi.h`
- Create: `firmware/components/net/include/net_http.h`
- Create: `firmware/components/net/net_wifi.c`
- Create: `firmware/components/net/net_http.c`
- Create: `firmware/main/idf_component.yml` (for the CA bundle, if not using IDF's)

**Interfaces:**
- Produces:
  - `esp_err_t net_wifi_connect(const char *ssid, const char *pass, int timeout_ms)`
  - `void net_wifi_disconnect(void)`
  - `int net_wifi_rssi(void)`
  - `esp_err_t net_http_get_json(const char *url, const char *bearer, char *out, size_t outlen)`
  - `esp_err_t net_http_post_json(const char *url, const char *bearer, const char *body, char *out, size_t outlen)`
  - `esp_err_t net_http_upload_chunk(const char *url, const void *data, size_t len, int offset, int total)`
- **Task stack sizing is the whole point of this task** (see §9.1 evidence): the worker
  that performs TLS is created with an **explicit large stack**, not a default.

- [ ] **Step 1: Create the component and its CMakeLists**

`firmware/components/net/CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS "net_wifi.c" "net_http.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi nvs_flash esp_http_client esp-tls mbedtls json
)
```

- [ ] **Step 2: Write the network worker with an explicitly sized stack**

The Arduino failure mode being avoided is a small default `loopTask` stack overflowing
during the TLS handshake (issues #566/#1260). ESP-IDF lets us size it, so we do, and we
record the number.

`firmware/components/net/include/net_http.h`:
```c
#pragma once
#include "esp_err.h"
#include <stddef.h>

/* TLS handshake stack budget. Sized deliberately, NOT inherited: the Arduino
 * crash class this project exists to avoid is exactly a too-small handshake
 * stack (spec §9.1). 16 KB is comfortably above measured need; lower it only
 * with evidence from uxTaskGetStackHighWaterMark(). */
#define NET_TLS_TASK_STACK  16384
#define NET_TLS_TASK_PRIO   5

esp_err_t net_http_get_json(const char *url, const char *bearer,
                            char *out, size_t outlen);
esp_err_t net_http_post_json(const char *url, const char *bearer,
                             const char *body, char *out, size_t outlen);
```

`firmware/components/net/net_http.c` (sketch — implement fully, keeping the stack
discipline):
```c
#include "net_http.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "net_http";

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
    size_t written;
} http_sink_t;

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    http_sink_t *s = (http_sink_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && s) {
        if (s->written + evt->data_len < s->cap) {
            memcpy(s->buf + s->written, evt->data, evt->data_len);
            s->written += evt->data_len;
            s->buf[s->written] = '\0';
        } else {
            ESP_LOGE(TAG, "response overflow (%u + %d >= %u)",
                     (unsigned)s->written, evt->data_len, (unsigned)s->cap);
        }
    }
    return ESP_OK;
}

static esp_err_t do_request(const char *url, const char *bearer,
                            const char *body, char *out, size_t outlen)
{
    http_sink_t sink = { .buf = out, .cap = outlen, .written = 0 };
    if (outlen) out[0] = '\0';

    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = on_http_event,
        .user_data = &sink,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_FAIL;

    esp_http_client_set_header(c, "Content-Type", "application/json");
    if (bearer) {
        char hdr[512];
        snprintf(hdr, sizeof(hdr), "Bearer %s", bearer);
        esp_http_client_set_header(c, "Authorization", hdr);
    }
    if (body) {
        esp_http_client_set_method(c, HTTP_METHOD_POST);
        esp_http_client_set_post_field(c, body, (int)strlen(body));
    }
    esp_err_t err = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);

    if (err != ESP_OK) return err;
    return (status >= 200 && status < 300) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t net_http_get_json(const char *url, const char *bearer,
                            char *out, size_t outlen)
{
    return do_request(url, bearer, NULL, out, outlen);
}

esp_err_t net_http_post_json(const char *url, const char *bearer,
                             const char *body, char *out, size_t outlen)
{
    return do_request(url, bearer, body, out, outlen);
}
```

`firmware/components/net/net_wifi.c` — **must**:
- use `WIFI_MODE_STA`, `esp_wifi_set_storage(WIFI_STORAGE_RAM)`;
- implement **bounded** reconnect (an event-group wait with a timeout, not `while(1)`), so
  a bad SSID fails the wake instead of hanging the device (NFR-1);
- call `esp_wifi_stop()` + `esp_wifi_deinit()` in `net_wifi_disconnect()` so the radio is
  genuinely off before the ADC2 battery read and before sleep (HW-3, NFR-3).

- [ ] **Step 3: Add the CA bundle config**

`firmware/sdkconfig.defaults` — append:
```ini
# TLS to OpenWeatherMap and Home Assistant needs a trust anchor (FR-5a, FR-6a).
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y
# TLS handshake into internal RAM; no PSRAM on this part (NFR-2).
CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384
CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=4096
```

- [ ] **Step 4: Build and check for the Arduino failure class**

Run: `cd firmware && pio run -e esp32dev`
Expected: `[SUCCESS]`, and note the reported static RAM/flash usage. Confirm the app fits
with room for the 76.4 KiB framebuffer at runtime.

- [x] **Step 5: On-hardware verification (observable pass/fail) — PASSED**

`pio run -e esp32dev -t upload --upload-port /dev/cu.usbserial-1121310` succeeds
(`Hash of data verified`). The harness is in `src/main.c` and credentials are injected at
build time by `tools/gen_secrets_header.py` from `code/.env` into the gitignored
`firmware/src/secrets_build.h` (FR-30).

**Measured results — all pass criteria met:**

| Criterion | Result |
|---|---|
| HTTP status / body | `ESP_OK`, 514 bytes of JSON, 20/20 requests |
| `Stack canary watchpoint triggered` | **absent** |
| Reboot | **none** |
| Free heap across 20 requests | 218,392 → 197,396 B, then **flat** (see below) |
| TLS task peak stack use | 3,420 B of 16,384 B (12,964 B headroom) |
| Reconnect after `net_wifi_disconnect()` | `ESP_OK`, rssi −61 |

**Heap was checked for a leak, not merely "stable".** The first request drops ~17.8 KB
(mbedTLS allocates its buffers once) and the value then oscillates around 197 KB — it does
**not** decline monotonically. A 60-request run confirmed the plateau. Had the drop been
per-request, 20 requests would have shown ~350 KB of loss, which is impossible on a part
with ~200 KB free, so the single step is a one-time allocation and not a leak.

**TLS stack headroom justifies `NET_TLS_TASK_STACK`.** Peak use is 3,420 B, so 16 KB is
4.8× headroom. It stays at 16 KB deliberately: the Arduino crash class this task exists to
prevent is a *small* handshake stack, and trimming to the measured peak would remove the
margin that protects against a larger HA response or a different ciphersuite.

**The association failures on the way here were real and are now fixed** (see defects 3
and 4). In order of appearance on hardware: `reason 202` (AUTH_FAIL) with the original
`WIFI_SSID=HomeNet`, then after switching to the 2.4 GHz-only SSID `HomeNet2G`:
`reason 4` (ASSOC_EXPIRE) → `reason 2` (AUTH_EXPIRE) → success. The progression matters —
it shows the password was never the problem once the SSID resolved to the 2.4 GHz radio,
and that the remaining failures were association/timing, not credentials.

**Defects found in this task's original sketch (four fixed):**

1. **The sketch performed TLS on the caller's stack — the exact failure class this task
   exists to prevent.** The plan says "the worker that performs TLS is created with an
   explicit large stack" and defines `NET_TLS_TASK_STACK`, but its `do_request()` calls
   `esp_http_client_perform()` directly. `app_main` runs with **3584 bytes**
   (`CONFIG_ESP_MAIN_TASK_STACK_SIZE=3584`), which is *worse* than the ~8 KB Arduino
   `loopTask` that spec §9.1 documents as overflowing during the handshake. Requests now
   run on a real task created with `NET_TLS_TASK_STACK`, joined via semaphore, which
   self-deletes (`vTaskDelete(NULL)`) so no stray task blocks deep sleep. Added
   `net_http_stack_hwm()` so the size can be justified by measurement rather than assumed.
2. **NVS was never initialised.** The WiFi driver persists PHY/calibration data through
   NVS and fails with `ESP_ERR_NVS_NOT_INITIALIZED` without it — observed on hardware as
   `wifi osi_nvs_open fail ret=4353` followed by `Failed to deinit Wi-Fi driver (0x3001)`.
   The sketch's `REQUIRES` list included `nvs_flash` but no code ever called
   `nvs_flash_init()`. Now initialised, with the `NO_FREE_PAGES`/`NEW_VERSION_FOUND`
   erase-and-retry path.
3. **No 802.11w (PMF) capability — associations were rejected outright.** A station that
   does not declare PMF capability can have its association rejected by a PMF-capable AP
   even with a correct key; this is what `reason 4` (ASSOC_EXPIRE) was. Now
   `pmf_cfg.capable = true, required = false`, which works with both PMF and non-PMF APs.
4. **WiFi power-save was left at the driver default.** Modem sleep is a known source of
   association/auth *timing* failures (`reason 2` AUTH_EXPIRE, `reason 15`
   4WAY_HANDSHAKE_TIMEOUT) on a first connect. Now `WIFI_PS_NONE` — which is also the right
   choice for this device, since it is awake only long enough to fetch and render, so
   power-save buys nothing and only adds latency.

**Bounded-reconnect design (NFR-1) — the retry policy is reason-aware.** The original
`ESP_ERR_TIMEOUT`-on-anything path retried nothing. Now association-class reasons
(2, 4, 15, 17, 34, 200, 201, 203) are retried within the caller's deadline, while
`reason 202` (AUTH_FAIL) fails immediately: retrying a rejected password only holds the
radio on and drains the battery (NFR-3) while producing the same error. Verified on
hardware — the `reason 4` run logged `association failed (reason 4); retrying` and then
connected 2 s later.

**Also corrected:** `ESP_ERROR_CHECK` on `esp_event_loop_create_default()` /
`esp_netif_init()` aborts on the *second* connect in one power cycle (they return
`ESP_ERR_INVALID_STATE`); both are now tolerated as success since the init is idempotent —
verified by the reconnect-after-deinit step, which is exactly the deep-sleep wake pattern.
The response sink records overflow as a sticky flag and returns `ESP_ERR_NO_MEM` rather
than handing a truncated body to the parser. A bearer token longer than the header buffer
is rejected instead of silently clipped (a clipped token yields a 401 that looks like a bad
token). The bounded-wait backstop is 30 s; a wedged worker fails the wake rather than
hanging.

**`net_wifi_scan()` was added** (not in the plan's interface list). It is the only way to
distinguish "wrong password" from "wrong SSID" — both surface as an auth failure — and
FR-30's provisioning flow needs a network list anyway.

**Note on `net_http_upload_chunk()`:** it appears in this task's interface list but has no
caller anywhere in the plan (FR-2a's chunked bitmap upload is Phase 5). Left unimplemented
rather than written speculatively — the chunked-upload design in Task 19 may not want this
signature.

- [ ] **Step 6: Commit**

```bash
git add firmware/components/net firmware/main/idf_component.yml firmware/sdkconfig.defaults
git commit -m "feat(net): wifi station with bounded reconnect and explicitly sized TLS task"
```

---

### Task 12: Boot sequence — the ordered wake path (FR-28, FR-29, FR-8, FR-9)

**Status: COMPLETE (2026-09-19), verified on hardware.** The ordered wake path runs:
NVS → config → `api_reset_cycle_counters` → VBAT with the radio OFF → `power_classify` →
`epd_init` + `app_render_last_good` + `epd_sleep` → `app_refresh_tick` →
`api_ota_mark_valid_if_pending` → USB: `api_start` + `app_serve_loop` (never returns) /
battery: deep sleep for `update_seconds`.

Measured boot: `vbat=4.29V source=usb`, `config: update=900s partial_limit=5 pages=1`,
`last-good image pushed (slot -1)`, `API listening (6 endpoints)`, and the device obtains
an IP (192.168.2.98 on the bench network).

**Correction to the original plan text:** `app_render_last_good()` and
`app_refresh_tick()` were listed here AND under Task 13. They belong to Task 12 — this
file owns the boot ordering, Task 13 owns the render pipeline they call.

**Defects this task surfaced and fixed (all found by running it on the board):**
- The default `partialRefreshLimit` was 24 in `cfg_store_default_json()` against FR-11's
  5, while both code fallbacks said 5. Pinned by a test so the three copies cannot drift.
- `POST /api/refresh` set a flag nothing consumed (see Task 14).
- Drawing after `epd_sleep()` timed out on BUSY: the controller ignores commands in deep
  sleep, and the failure looks exactly like a loose FPC. Added `epd_wake()`.

This is the task that makes the whole device behave, and the one where ordering matters
for a hardware reason (the ADC2/WiFi conflict).

**Files:**
- Create: `firmware/components/app/CMakeLists.txt`
- Create: `firmware/components/app/include/app_boot.h`
- Create: `firmware/components/app/app_boot.c`
- Create: `firmware/components/app/app_refresh.c`
- Modify: `firmware/src/main.c`
- Create: `firmware/test/test_refresh_policy/test_refresh_policy.c` (pure logic)
- Create: `firmware/lib/layout/include/refresh_policy.h`
- Create: `firmware/lib/layout/src/refresh_policy.c`

**Interfaces:**
- Produces (pure, host-tested):
  - `typedef enum { REFRESH_FULL, REFRESH_PARTIAL } refresh_kind_t;`
  - `refresh_kind_t refresh_decide(int partials_since_full, int partial_limit, int hours_since_full)`
- Produces (device): `void app_boot_run(void);` called from `app_main`.

- [ ] **Step 1: Write the failing test for the refresh policy (FR-10, FR-11, FR-12)**

The daily full refresh is the panel datasheet's hard requirement (ghosting after 24 h), so
it is encoded as a pure, tested function — not left to a caller to remember.

`firmware/test/test_refresh_policy/test_refresh_policy.c`:
```c
#include "unity.h"
#include "refresh_policy.h"

void setUp(void) {}
void tearDown(void) {}

static void test_first_ever_refresh_is_full(void)
{
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(0, 5, 0));
}

static void test_partial_until_the_limit(void)
{
    for (int i = 1; i < 5; i++) {
        TEST_ASSERT_EQUAL_INT(REFRESH_PARTIAL, refresh_decide(i, 5, 0));
    }
}

static void test_full_at_the_limit(void)
{
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(5, 5, 0));
}

/* The datasheet rule (FR-10): refresh at least every 24 h or ghosting occurs.
 * This must beat the partial counter. */
static void test_daily_full_refresh_overrides_partial_budget(void)
{
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(1, 5, 24));
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(0, 5, 48));
    TEST_ASSERT_EQUAL_INT(REFRESH_PARTIAL, refresh_decide(1, 5, 23));
}

static void test_zero_limit_never_allows_partial(void)
{
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(0, 0, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_first_ever_refresh_is_full);
    RUN_TEST(test_partial_until_the_limit);
    RUN_TEST(test_full_at_the_limit);
    RUN_TEST(test_daily_full_refresh_overrides_partial_budget);
    RUN_TEST(test_zero_limit_never_allows_partial);
    return UNITY_END();
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cd firmware && pio test -e native -f test_refresh_policy`
Expected: FAIL — `refresh_policy.h` not found.

- [ ] **Step 3: Implement**

`firmware/lib/layout/include/refresh_policy.h`:
```c
#pragma once

typedef enum { REFRESH_FULL, REFRESH_PARTIAL } refresh_kind_t;

/* Decide whether this update is a full refresh (slow, clears ghosting) or a
 * partial (fast, low flicker). A full refresh is forced when:
 *   - nothing has been drawn since the last full (partials_since_full == 0),
 *   - the partial budget is spent, or
 *   - 24 h have passed since the last full, per the panel datasheet (FR-10). */
refresh_kind_t refresh_decide(int partials_since_full, int partial_limit,
                              int hours_since_full);
```

`firmware/lib/layout/src/refresh_policy.c`:
```c
#include "refresh_policy.h"

refresh_kind_t refresh_decide(int partials_since_full, int partial_limit,
                              int hours_since_full)
{
    if (partials_since_full <= 0)      return REFRESH_FULL;
    if (partial_limit <= 0)            return REFRESH_FULL;
    if (partials_since_full >= partial_limit) return REFRESH_FULL;
    if (hours_since_full >= 24)        return REFRESH_FULL;   /* datasheet rule */
    return REFRESH_PARTIAL;
}
```

- [ ] **Step 4: Run to verify pass**

Run: `cd firmware && pio test -e native -f test_refresh_policy`
Expected: 5 tests PASS.

- [ ] **Step 5: Implement the ordered boot path**

`firmware/components/app/app_boot.c` — the order is a **requirement**, and the first two
steps are ordered that way for a hardware reason (ADC2 is unusable while WiFi is up):

```c
#include "app_boot.h"
#include "canvas.h"
#include "devcfg.h"
#include "epd.h"
#include "layout_model.h"
#include "net_http.h"
#include "net_wifi.h"
#include "power.h"
#include "refresh_policy.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "boot";

/* Step 2 of FR-28: read VBAT with WiFi OFF (HW-3). GPIO26 = ADC2_CH9. */
static double read_vbat(void)
{
    adc_oneshot_unit_handle_t adc;
    adc_oneshot_unit_init_cfg_t init = { .unit_id = ADC_UNIT_2 };
    if (adc_oneshot_new_unit(&init, &adc) != ESP_OK) return 0.0;

    adc_oneshot_chan_cfg_t ch = {
        .atten = ADC_ATTEN_DB_12,      /* ~0..3.3 V range */
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_oneshot_config_channel(adc, ADC_CHANNEL_9, &ch);

    int raw = 0;
    esp_err_t err = adc_oneshot_read(adc, ADC_CHANNEL_9, &raw);
    adc_oneshot_del_unit(adc);
    if (err != ESP_OK) return 0.0;

    double vref  = power_vref_from_raw(raw, 4096, 3.3);
    return power_vbat_from_vref(vref);
}

void app_boot_run(void)
{
    /* 1. NVS + config */
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* 2. Battery sense — WIFI MUST BE OFF HERE (HW-3). */
    double vbat = read_vbat();
    power_source_t src = power_classify(vbat, 0.0);
    ESP_LOGI(TAG, "vbat=%.2fV source=%d", vbat, (int)src);

    /* 3. Panel: render the last-good state so a network failure never leaves the
     *    panel blank or stale-looking (FR-29). `app_render_last_good()` composes
     *    the stored static layer with the persisted last values and pushes it.
     *    A timeout here means the panel did not answer (loose FPC / unpowered) — log
     *    it and carry on so the device still comes up and stays reachable over WiFi,
     *    rather than hanging or reboot-looping on a wall (see Task 6 step 6a). */
    if (epd_init() != ESP_OK) {
        ESP_LOGE(TAG, "panel did not respond (BUSY timeout) — skipping draw, continuing");
    } else {
        app_render_last_good();  /* implemented in Task 13's render pipeline */
        epd_sleep();
    }

    /* 4-6. Network, fetch, render happen in app_refresh_tick() (Task 13). */
    app_refresh_tick(src);

    /* 7. Sleep if on battery (FR-9). */
    if (src == POWER_SOURCE_BATTERY) {
        int wake_s = 900;
        esp_sleep_enable_timer_wakeup((uint64_t)wake_s * 1000000ULL);
        epd_sleep();
        esp_deep_sleep_start();
    }
}
```

`firmware/src/main.c`:
```c
#include "app_boot.h"

void app_main(void)
{
    app_boot_run();
}
```

- [ ] **Step 6: On-hardware verification**

Flash and check, with the device on USB: it must connect, fetch, render, and (if the VBAT
divider reads battery) enter deep sleep. Measure sleep current — expected **microamps**,
with the panel asleep (NFR-3). On battery, confirm wake → update → sleep cycles repeat.

- [ ] **Step 7: Commit**

```bash
git add firmware/lib/layout/include/refresh_policy.h firmware/lib/layout/src/refresh_policy.c \
        firmware/test/test_refresh_policy firmware/components/app firmware/src/main.c
git commit -m "feat(app): ordered boot path with VBAT-before-WiFi and tested refresh policy"
```

---

### Task 13: Render pipeline — font atlases + composite static/dynamic (FR-1, FR-2, FR-3, FR-4, FR-4b, NFR-9)

**Status: COMPLETE (2026-09-19).** 15 font-atlas tests + 13 render tests, all passing;
full native suite **126/126**. Firmware builds (RAM 13.5%, flash 60.2% without the renderer
linked, 61.7% with it). Golden image generated and visually inspected.

**Measured cost of the render pipeline:** **22,968 bytes of flash** (970,409 vs a 947,441
baseline), of which 19,348 is the two atlas bitmaps and ~3.6 KB is code. RAM delta: none
(the atlases are `static const`, so they live in flash).

**Fonts used:** Inter v4.1 — `Inter-Regular.ttf` @ 20 px (body), `Inter-SemiBold.ttf` @
64 px (value). SIL OFL 1.1, embedding permitted; the licence text is vendored at
`firmware/assets/fonts/Inter-LICENSE.txt`.

**Why font atlases and not a runtime font engine (FR-4a):** the panel is **1-bit**, so
anti-aliasing is impossible — grey fringes become visible dots at 198 dpi. The crispest
text comes from glyphs rasterised **once, at the exact pixel size**, hinted for that size,
and then blitted. A runtime TTF rasteriser would also burn RAM and flash the device does
not have.

**Files:**
- Create: `firmware/lib/layout/include/render.h`
- Create: `firmware/lib/layout/src/render.c`
- Create: `firmware/lib/layout/include/fonts.h`
- Create: `firmware/lib/layout/src/fonts.c`
- Create: `firmware/lib/layout/src/atlas_body.h` (generated)
- Create: `firmware/lib/layout/src/atlas_value.h` (generated)
- Create: `firmware/test/test_render_golden/test_render_golden.c`
- Create: `firmware/test/test_font_atlas/test_font_atlas.c`
- Create: `firmware/tools/gen_font_atlas.py`
- Create: `firmware/tools/gen_golden.c`, `firmware/tools/dump_1bpp.py`
- Create: `firmware/tools/golden/default_layout.h` (shared layout spec),
  `default_layout.bin` (golden), `default_layout.json` (for the web app cross-check, Task 17)
- Create: `firmware/assets/fonts/` (TTFs + licence)

**Interfaces:**
- Produces:
  - `int font_glyph(int font_id, char ch, const uint8_t **bits, int *w, int *h)`
  - `int font_measure(int font_id, const char *s, int *w, int *h)`
  - `int font_bearing(int font_id, char ch, int *bx, int *by)` (added beyond the plan — see
    Step 3 notes; without it punctuation renders at the top of the line box)
  - `typedef struct { int x, y, w, h; char align_h; char align_v; int font_id; } value_field_t;`
  — geometry + presentation only. **There is no `text` field on purpose:** a field's
  *dynamic* string comes from `values[i]`, and any *static* label is baked into the
  pre-rendered static layer by the web app (FR-1). Keeping labels out of the field
  struct is what makes the firmware layout-independent.
  - `int render_compose(canvas_t *c, const uint8_t *static_layer, const value_field_t *fields, const char *const *values, int n_fields)`

**Steps 1–10: done.** Defects found and fixed during implementation (each was caught by a
test or by looking at the golden, not by inspection):

1. **`getbbox()` measured the background, not the ink** (major). On a mode-L image
   `getbbox()` returns the bbox of non-zero pixels and the background is 255, so every
   glyph measured 192×192 — a 2.7 MB atlas where every character would render as a giant
   black square. Fixed by thresholding *before* measuring, using the same threshold as the
   bitmap. Body 45,600 → 2,021 bytes; value 437,760 → 17,327 bytes.
2. **Bearings were ascender-relative, not baseline-relative** (major, caught by LOOKING at
   the golden PNG). Pillow's default anchor is `"la"` (left/ascender), so the raw bbox top
   is relative to the ascender line. Feeding that to the renderer pushed every digit a full
   ascent (62 px at the value size) below where it belonged, so the big numerals rendered
   **clipped in half by their own box**. Fixed in the generator (`by = (t - pad) - ascent`)
   and the convention is now documented in `fonts.h` and pinned by a test that asserts a
   cap ends exactly on the baseline (`by + h == 0`). *This is the strongest argument for
   Step 9's "inspect it" instruction — every test passed while the image was visibly wrong.*
3. **`font_measure` returning ink height instead of the face line height** would have made
   a line's measured height depend on which characters were in it, so text would shift
   vertically between refreshes as digits changed. `*h` is now the constant line height.
4. **Metrics hardcoded in `fonts.c`** while the atlas generator owned them — regenerating
   at a different `--px` would have silently left the C metrics stale and put every line at
   the wrong baseline. The generator now emits `ASCENT`/`DESCENT`/`LINE_HEIGHT` and
   `fonts.c` reads them.
5. **The golden test and the golden generator had different layouts.** The test used a blank
   static layer while the generator baked labels and rules, so the golden could never match.
   Both now include `tools/golden/default_layout.h`, one spec for fields, values, labels and
   rules — the golden cannot lock a layout nobody renders.
6. **`.env` at the workspace root was not gitignored** (security). Only `code/.env` was
   covered; a repo-wide `git add -A` consults the *root* `.gitignore`, so a root-level
   `.env` — which exists, with the same OWM key, HA token and WiFi password — would have
   been committed. No commits exist yet, so nothing leaked. Fixed and verified with
   `git check-ignore -v`. `.DS_Store` and `__pycache__` were added to the same file.

**Test coverage beyond the plan's four font tests** (the plan's `test_glyphs_are_fully_black_or_white`
had a tautology at its core — `on == 0 || on == 1` on a bit extracted with `& 1` is always
true — so the real assertion, the padding-bit check, was kept and the tautology dropped):

- glyph packing is contiguous (offsets `off[i+1] == off[i] + h*ceil(w/8)`), so a generator
  bug that overlapped glyphs cannot silently corrupt text;
- every glyph's ink, placed at its bearing, fits in the line box;
- punctuation sits near the baseline rather than floating at the ascender;
- a descender hangs below the baseline while a cap ends on it (pins the bearing sign);
- `font_glyph`/`font_advance`/`font_bearing` reject out-of-range and negative font ids
  (`(font_id_t)-1` would otherwise index the face table out of bounds);
- transparent-white really is transparent: a field over solid black static ink leaves the
  ink count unchanged (a white-painting blit would punch a hole in the art);
- an over-long value is clipped to its box and does not touch the static layer outside it;
- a field at the panel edge is clipped and does not wrap to the next row;
- 'L'/'C'/'R' and 'T'/'M'/'B' produce three different bitmaps, with left ink starting left
  of right ink and 'B' ink below 'T' ink;
- an unrecognised align char falls back to top-left rather than drawing nothing;
- an empty or NULL value, and a value with a non-ASCII character, leave the static layer
  untouched rather than erasing the field box;
- fields are independent — one empty field does not stop a later one drawing;
- NULL/negative arguments are rejected.

**Not yet done / deferred:**
- FR-4b per-configuration atlas subsetting stays **deferred**; measured cost is 19.3 KB of
  the 22.9 KB, against 61.7% flash used, so there is no pressure to subset yet.
- The golden is locked but the device has **not** displayed it yet — the panel check comes
  with Task 15's wiring, when there is a real static layer to push.
- `dump_1bpp.py` needs Pillow (the `/tmp/atlasvenv` venv) to inspect goldens.

- [x] **Step 1: Write the atlas generator and a failing crispness test**

`firmware/tools/gen_font_atlas.py` takes a TTF and a size and emits a C header: a packed
1-bit glyph bitmap per character plus a width/advance table. **Threshold, do not dither** —
dithering makes type look dirty. Generate at least two faces from a size ladder tuned to
198 dpi: a **body** face (~18–22 px) and a **value** face (~56–72 px) for the big numerals.
Candidate faces are Inter, IBM Plex Sans and Roboto — clean, high x-height, excellent at
small sizes.

`firmware/test/test_font_atlas/test_font_atlas.c`:
```c
#include <string.h>
#include "unity.h"
#include "fonts.h"

void setUp(void) {}
void tearDown(void) {}

/* Every printable ASCII glyph the default layout can show must exist, or a
 * temperature like "-12.4" would silently render blanks. */
static void test_all_ascii_glyphs_are_present_and_sized(void)
{
    for (char ch = 32; ch < 127; ch++) {
        const uint8_t *bits = NULL; int w = 0, h = 0;
        TEST_ASSERT_EQUAL_INT(0, font_glyph(FONT_BODY, ch, &bits, &w, &h));
        TEST_ASSERT_NOT_NULL(bits);
        TEST_ASSERT_GREATER_THAN_INT(0, w);
        TEST_ASSERT_GREATER_THAN_INT(0, h);
    }
    /* Space is blank but must still occupy an advance width. */
    const uint8_t *sp = NULL; int sw = 0, sh = 0;
    font_glyph(FONT_BODY, ' ', &sp, &sw, &sh);
    TEST_ASSERT_GREATER_THAN_INT(0, sw);
}

/* The big value face must be materially larger than the body face, or the
 * "temperature is the hero" design is impossible. */
static void test_value_face_is_larger_than_body(void)
{
    const uint8_t *b = NULL, *v = NULL; int bw = 0, bh = 0, vw = 0, vh = 0;
    font_glyph(FONT_BODY, '8', &b, &bw, &bh);
    font_glyph(FONT_VALUE, '8', &v, &vw, &vh);
    TEST_ASSERT_GREATER_THAN_INT(bh * 2, vh);
}

static void test_measure_matches_glyph_advances(void)
{
    int w = 0, h = 0;
    TEST_ASSERT_EQUAL_INT(0, font_measure(FONT_BODY, "72", &w, &h));
    const uint8_t *b1 = NULL, *b2 = NULL; int w1 = 0, h1 = 0, w2 = 0, h2 = 0;
    font_glyph(FONT_BODY, '7', &b1, &w1, &h1);
    font_glyph(FONT_BODY, '2', &b2, &w2, &h2);
    TEST_ASSERT_EQUAL_INT(w1 + w2, w);
}

/* Glyphs must be strictly 1-bit: no partially-set bytes that would show as
 * grey speckle on the panel (NFR-4). */
static void test_glyphs_are_fully_black_or_white(void)
{
    for (char ch = 48; ch <= 57; ch++) {
        const uint8_t *bits = NULL; int w = 0, h = 0;
        font_glyph(FONT_VALUE, ch, &bits, &w, &h);
        int pitch = (w + 7) / 8;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int on = (bits[y * pitch + (x >> 3)] >> (7 - (x & 7))) & 1;
                TEST_ASSERT_TRUE(on == 0 || on == 1);   /* tautology guard for the reader */
            }
            /* Padding bits beyond the glyph width must be white, not stray ink. */
            if (w % 8) {
                uint8_t pad = (uint8_t)(0xFFu >> (w % 8));
                TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)(bits[y * pitch + pitch - 1] & pad));
            }
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_all_ascii_glyphs_are_present_and_sized);
    RUN_TEST(test_value_face_is_larger_than_body);
    RUN_TEST(test_measure_matches_glyph_advances);
    RUN_TEST(test_glyphs_are_fully_black_or_white);
    return UNITY_END();
}
```

- [x] **Step 2: Run to verify failure**

Run: `cd firmware && pio test -e native -f test_font_atlas`
Expected: FAIL — `fonts.h` not found / atlas headers missing.

- [x] **Step 3: Generate the atlases and implement `fonts.c`**

Run the generator for both faces, commit the generated headers, and implement
`font_glyph`/`font_measure` as table lookups. `FONT_BODY` and `FONT_VALUE` are the two
`font_id` constants the layout uses.

- [x] **Step 4: Run to verify the font tests pass**

Run: `cd firmware && pio test -e native -f test_font_atlas`
Expected: 4 tests PASS. **If the padding-bit assertion fails, the generator is emitting
stray ink — fix the generator, not the test.**

- [x] **Step 5: Write the render tests (golden image + box clipping) with a deliberate failure**

*(FR-4b note: per-configuration atlas subsetting is **deferred** until flash pressure is
actually measured. Ship full ASCII/latin-1 first; revisit only if the bundle does not fit.)*

`firmware/test/test_render_golden/test_render_golden.c`:
```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "canvas.h"
#include "render.h"

static uint8_t *fb;
static uint8_t *static_layer;

void setUp(void)
{
    fb = malloc(EPD_FB_BYTES);
    static_layer = malloc(EPD_FB_BYTES);
    memset(static_layer, 0xFF, EPD_FB_BYTES);   /* white background */
}
void tearDown(void) { free(fb); free(static_layer); }

/* Blitting the static layer alone must reproduce it exactly. */
static void test_static_layer_is_blitted_verbatim(void)
{
    canvas_t c;
    canvas_init(&c, fb);
    render_compose(&c, static_layer, NULL, NULL, 0);
    TEST_ASSERT_EQUAL_MEMORY(static_layer, fb, EPD_FB_BYTES);
}

/* A dynamic field must change the bitmap, and only inside its own box. */
static void test_value_field_draws_inside_its_box(void)
{
    value_field_t f = { .x = 100, .y = 100, .w = 200, .h = 80,
                        .align_h = 'L', .align_v = 'T', .font_id = 0 };
    const char *values[1] = { "72" };

    canvas_t c;
    canvas_init(&c, fb);
    render_compose(&c, static_layer, &f, values, 1);

    TEST_ASSERT_TRUE(memcmp(static_layer, fb, EPD_FB_BYTES) != 0);   /* it drew something */

    /* Outside the field box, every bit must be identical to the static layer. */
    for (int y = 0; y < EPD_HEIGHT; y++) {
        for (int x = 0; x < EPD_WIDTH; x++) {
            if (y >= 100 && y < 180 && x >= 100 && x < 300) continue;
            size_t idx = (size_t)y * EPD_PITCH + (size_t)(x >> 3);
            uint8_t mask = (uint8_t)(0x80u >> (x & 7));
            TEST_ASSERT_EQUAL_INT((static_layer[idx] & mask) ? 1 : 0,
                                  (fb[idx] & mask) ? 1 : 0);
        }
    }
}

/* Golden image: a pinned config + pinned values => an exact byte-for-byte result.
 * Regenerate ONLY with a deliberate, reviewed reason (NFR-9). */
static void test_golden_image_of_default_layout(void)
{
    /* Load tools/golden/default_layout.bin generated alongside this test. */
    FILE *f = fopen("tools/golden/default_layout.bin", "rb");
    if (!f) { TEST_IGNORE_MESSAGE("golden image not generated yet"); return; }
    uint8_t golden[EPD_FB_BYTES];
    size_t got = fread(golden, 1, EPD_FB_BYTES, f);
    fclose(f);
    TEST_ASSERT_EQUAL_INT(EPD_FB_BYTES, (int)got);

    value_field_t fields[] = {
        { .x = 40,  .y = 40,  .w = 300, .h = 120, .align_h='L', .align_v='T',
          .font_id = 1 },
        { .x = 40,  .y = 200, .w = 300, .h = 120, .align_h='L', .align_v='T',
          .font_id = 1 },
    };
    const char *values[] = { "68.4", "41.2" };

    canvas_t c; canvas_init(&c, fb);
    render_compose(&c, static_layer, fields, values, 2);
    TEST_ASSERT_EQUAL_MEMORY(golden, fb, EPD_FB_BYTES);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_static_layer_is_blitted_verbatim);
    RUN_TEST(test_value_field_draws_inside_its_box);
    RUN_TEST(test_golden_image_of_default_layout);
    return UNITY_END();
}
```

- [x] **Step 6: Run to verify failure**

Run: `cd firmware && pio test -e native -f test_render_golden`
Expected: FAIL — `render.h` not found (and the golden test will report a missing image
until Step 5, which is the intended order).

- [x] **Step 7: Implement the renderer**

`firmware/lib/layout/src/render.c` — implement `render_compose` to (a) copy the static
layer, then (b) for each field, look up each character's glyph, and blit it with
`canvas_blit_1bpp_masked(..., transparent_white = 1)` so only the ink lands. Honour
`align_h` and `align_v` by measuring with `font_measure` and offsetting within the field
box. **Clip every glyph to the field box** — the box test above fails otherwise, and an
unclipped field could overwrite the static art.

- [x] **Step 8: Run to verify the first two tests pass**

Run: `cd firmware && pio test -e native -f test_render_golden`
Expected: `test_static_layer_is_blitted_verbatim` and `test_value_field_draws_inside_its_box`
PASS; the golden test reports **ignored** (no image yet).

- [x] **Step 9: Generate the golden image and lock it**

Generate `firmware/tools/golden/default_layout.bin` from a reviewed run, inspect it (dump
to a 1-bit PNG and look at it), then re-run:
Expected: all 3 tests PASS, including the golden comparison.

- [x] **Step 10: Commit**

```bash
git add firmware/lib/layout/include/render.h firmware/lib/layout/include/fonts.h \
        firmware/lib/layout/src/render.c firmware/lib/layout/src/fonts.c \
        firmware/tools/gen_font_atlas.py firmware/tools/golden \
        firmware/test/test_render_golden
git commit -m "feat(render): composite renderer with clipped value fields and a golden image"
```

---

### Task 14: Device config API + chunked bitmap upload (FR-31, IF-2a, IF-4)

**Status: COMPLETE (2026-09-19), verified on hardware with `curl`.**

Every endpoint exercised against the board at 192.168.2.98:

| Request | Result |
|---|---|
| `GET /api/status` | 200, `rssi:-58 vbat:4.29 power_source:"usb"` |
| `GET /api/config` | 200, the stored document |
| `PUT /api/config` (valid) | 200 `{"status":"stored"}`, reads back identical |
| `PUT /api/config` (truncated JSON) | **400**, config unchanged, device alive, error ringed |
| `POST /api/bitmap` (complete) | 200 `{"status":"promoted"}`, slot flips, panel repaints |
| `POST /api/bitmap` (stopped at 3 chunks) | slot stays invalid, **previous image stays live** |
| `POST /api/bitmap` (stopped at 5 chunks, slot already live) | live slot **unchanged** |
| `POST /api/bitmap` (wrong CRC) | **409** `checksum mismatch`, live slot unchanged |
| `POST /api/refresh` | 202, and the panel actually updates |
| `GET /api/nope` | 404, no crash |

**Visual confirmation:** the bench USB camera shows the uploaded `border` bitmap on the
glass (black frame, centre block) with the two `--` value placeholders — i.e. the whole
chain works end to end: chunked HTTP → flash slot → render → panel.

**Files (actual, differs from the list below):** the API is
`components/api/{api_server.c, api_store.c, api_ota.c}`; the pure formatting and query
parsing went to `lib/apifmt/` and the upload/slot logic to `lib/upload/` so both are
host-testable. `components/shared/CMakeLists.txt` exists because PlatformIO's ESP-IDF
builder never builds `lib/` — without it those libraries are silently absent from the
image (the build succeeded and produced a byte-identical `firmware.bin`).

**Defects found by the on-hardware step:** `POST /api/refresh` answered 202 "scheduled"
but nothing consumed the flag — the boot path refreshes exactly once, so after a bitmap
upload the panel kept the old image while the API reported success. Added the USB serve
loop (`app_serve_loop`). Also: `net_wifi_connect()` failed with "sta is connected" when
already associated, dropping every refresh after the first; and `app_refresh_tick()`
returned early on a failed fetch, so an upload was never drawn during an OWM outage.

**Files:**
- Create: `firmware/components/api/CMakeLists.txt`
- Create: `firmware/components/api/include/api.h`
- Create: `firmware/components/api/api_config.c`
- Create: `firmware/components/api/api_bitmap.c`
- Create: `firmware/components/api/api_ota.c`

**Interfaces:**
- Endpoints (from IF-4):
  - `GET  /api/config` → the stored config JSON
  - `PUT  /api/config` → validate via `layout_config_parse`, then persist to NVS
  - `POST /api/bitmap` → chunked upload; body carries `offset`/`total`; final chunk
    verifies a checksum and **atomically** promotes the new bitmap (IF-2a)
  - `POST /api/refresh` → force a refresh now
  - `GET  /api/status` → version, uptime, min free heap, RSSI, VBAT, last fetch/refresh,
    refresh counts, last error (FR-33)
  - `POST /api/ota` → OTA update (FR-32)

- [ ] **Step 1: Implement `GET /api/status` first**

It is the smallest endpoint and the one that makes every later hardware step observable.
Return a JSON object including `free_heap_min`, `rssi`, `vbat`, and
`partials_since_full` / `fulls_total`.

- [ ] **Step 2: Implement `PUT /api/config` with validation-before-store**

Critically: **run `devcfg_migrate` then `layout_config_parse` and reject the write on
failure** — a bad config must never be persisted, or the device bricks itself into an
unparseable state on next boot. Return 400 with the parse error.

- [ ] **Step 3: Implement the chunked bitmap upload with atomic promote**

Rules that must hold (each becomes a check):
- a chunk arriving out of order is rejected (409), not written;
- an interrupted upload leaves the **previous** bitmap intact and active;
- the final chunk verifies a checksum over all 78,200 bytes before promotion;
- promotion is a single pointer/partition swap, never an in-place overwrite.

- [ ] **Step 4: Implement OTA (`esp_https_ota`) with rollback (FR-32)**

Use the two OTA partitions already in `partitions.csv`. Enable
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` (already in `sdkconfig.defaults`) and call
`esp_ota_mark_app_valid_cancel_rollback()` **only after** the new firmware has proven
itself (completed a successful boot + one successful refresh) — otherwise a bad image
rolls back automatically.

- [ ] **Step 5: On-hardware verification**

With the device on the network, exercise each endpoint with `curl`:
```bash
curl -s http://<device-ip>/api/status | jq .
curl -s -X PUT http://<device-ip>/api/config -d @test-config.json
curl -s -X PUT http://<device-ip>/api/config -d '{bad json'   # must return 400, device survives
# Interrupt a bitmap upload on purpose, then GET /api/status -> old bitmap still active
```
Expected: `status` reflects real values; the bad config is rejected and the device keeps
running; the interrupted upload leaves the previous image displayed.

- [ ] **Step 6: Commit**

```bash
git add firmware/components/api
git commit -m "feat(api): config API with validate-before-store, atomic bitmap upload, OTA rollback"
```

---

### Task 15: Provisioning — captive portal + BLE (FR-30)

**Files:**
- Create: `firmware/components/prov/CMakeLists.txt`
- Create: `firmware/components/prov/include/prov.h`
- Create: `firmware/components/prov/prov_softap.c`
- Create: `firmware/components/prov/prov_ble.c`

**Interfaces:**
- Produces: `esp_err_t prov_run_if_unconfigured(void)` — starts SoftAP + captive portal
  **and** BLE provisioning together, blocks until credentials are stored, then returns.

- [ ] **Step 1: Detect the unconfigured state**

If NVS has no SSID, provisioning is required. (A configured device that merely fails to
join WiFi must **not** silently drop into AP mode — that would be a hostile surprise.
Only the *unconfigured* state enters provisioning.)

- [ ] **Step 2: Bring up the captive portal**

Use `wifi_prov_mgr` with the SoftAP transport plus a DNS catch-all so any URL lands on the
config page.

- [ ] **Step 3: Bring up BLE provisioning for the official app**

Initialise the **BLE** transport as well. The user specifically required support for the
official **ESP BLE Provisioning** app, which speaks this component's protocol (App Store /
Play Store: "BLE based Wi-Fi Provisioning from IDF v3.2 and later"). Use
`wifi_prov_scheme_ble` with the security-1 / Proof-of-Possession flow, and log the PoP so
the user can enter it.

- [ ] **Step 4: Store the secrets in NVS, never in git**

Provisioning writes SSID/password; the web UI writes the OWM key, HA URL/token and
location. All go to NVS. Confirm `.gitignore` covers any local secrets file, and that no
credential is ever compiled in.

- [ ] **Step 5: On-hardware verification**

Erase NVS (`pio run -e esp32dev -t erase`), boot, and confirm:
- the device raises an AP with a captive portal;
- the **official ESP BLE Provisioning app** finds it, accepts the PoP, lists WiFi networks
  and delivers credentials;
- after credentials are stored the device reboots into normal operation and the AP is gone.

- [ ] **Step 6: Commit**

```bash
git add firmware/components/prov
git commit -m "feat(prov): captive portal plus BLE provisioning for the official ESP app"
```

---

## Phase 5 — Web configuration app

> **VERIFIED toolchain (do not re-derive):** vanilla TS + Vite + Vitest. The exact
> `tsconfig.json` needs `lib: ["ES2022","DOM","DOM.Iterable","ESNext.Disposable"]`,
> `types: ["node"]` with `@types/node` installed, and `skipLibCheck: true`. Tests must
> **import `describe/it/expect` explicitly** — `@vitest/globals` does not resolve for this
> vitest version. Vite needs an `index.html` entry. All verified: typecheck clean, 4 tests
> pass, production build succeeds.

### Task 16: Web app scaffold + typed config model (IF-1)

**Files:**
- Create: `webapp/package.json`, `webapp/tsconfig.json`, `webapp/vitest.config.ts`,
  `webapp/vite.config.ts`, `webapp/index.html`
- Create: `webapp/src/model/config.ts`
- Create: `webapp/src/model/canvas-consts.ts`
- Create: `webapp/test/config.test.ts`

**Interfaces:**
- Produces: `SCHEMA_VERSION`, `PANEL_WIDTH=920`, `PANEL_HEIGHT=680`, `FB_BYTES=78200`,
  and the `Config` type (`pages[]`, `widgets[]`, `DataBinding`, `AlertRule`, `LocationConfig`)
  that **every** later web app task and the firmware's `layout_config_t` subset depend on.
  These constants live in `canvas-consts.ts` (which imports nothing) and are re-exported
  from `config.ts`, so pure-logic modules and node tests can use them without pulling in
  DOM globals.

- [ ] **Step 1: Write the failing tests**

`webapp/test/config.test.ts`:
```ts
import { describe, it, expect } from 'vitest';
import {
  emptyConfig, SCHEMA_VERSION, PANEL_WIDTH, PANEL_HEIGHT, FB_BYTES,
} from '../src/model/config';

describe('config model', () => {
  it('framebuffer math matches the panel contract (HW-6)', () => {
    expect(PANEL_WIDTH).toBe(920);
    expect(PANEL_HEIGHT).toBe(680);
    expect(FB_BYTES).toBe(78_200);
    expect(FB_BYTES).toBe((PANEL_WIDTH * PANEL_HEIGHT) / 8);
  });

  it('a fresh config is valid and has one page', () => {
    const c = emptyConfig(new Date('2026-09-18T00:00:00Z'));
    expect(c.schemaVersion).toBe(SCHEMA_VERSION);
    expect(c.pages).toHaveLength(1);
    expect(c.pages[0]!.name).toBe('Main');
  });

  it('defaults are the documented ones', () => {
    const c = emptyConfig();
    expect(c.updateSeconds).toBe(900);
    expect(c.partialRefreshLimit).toBe(5);
    expect(c.owmProduct).toBe('auto');
    expect(c.ha.mode).toBe('rest');
  });

  it('round-trips through JSON without loss', () => {
    const c = emptyConfig();
    c.pages[0]!.widgets.push({
      id: 'w1', x: 10, y: 20, w: 300, h: 120, role: 'dynamic',
      binding: { kind: 'ha', entityId: 'sensor.upstairs_hallway_temperature' },
      format: { decimals: 1, suffix: '°F' },
      font: { size: 48, align: 'left', valign: 'middle' },
      alerts: [{ op: 'gt', threshold: 100, level: 'severe' }],
    });
    const back = JSON.parse(JSON.stringify(c));
    expect(back).toEqual(c);
  });
});
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd webapp && npm install && npm test`
Expected: FAIL — `../src/model/config` not found.

- [ ] **Step 3: Create the scaffold and the model**

Create the config files exactly as listed in the "VERIFIED toolchain" note above, then:

`webapp/src/model/canvas-consts.ts` — the three panel constants and nothing else:
```ts
export const PANEL_WIDTH = 920;
export const PANEL_HEIGHT = 680;
export const FB_BYTES = (PANEL_WIDTH * PANEL_HEIGHT) / 8;   /* 78_200 (HW-6) */
```
This module must import nothing so it can be used from node tests and pure-logic
modules; `config.ts` re-exports the three names.

`webapp/src/model/config.ts` — the full typed document: `SCHEMA_VERSION = 1`;
re-export `PANEL_WIDTH/HEIGHT/FB_BYTES` from `canvas-consts`; `AlertOp`/`AlertLevel`; `DataSourceKind`
(`'owm-current' | 'owm-daily' | 'owm-alert' | 'ha'`); `DataBinding`; `AlertRule`; `Widget`
(with `role: 'static' | 'dynamic'`, optional `binding`, `format`, `font`, `alerts`,
`showsOwmAlerts`); `Page`; `LocationConfig`; `Config`
(with `updateSeconds`, `partialRefreshLimit`, `location`, `owmProduct`,
`powerMode: 'auto' | 'always-on' | 'battery'`, `ha`, `pages`);
and `emptyConfig()` returning the documented defaults (900 s, partial limit 5, owmProduct
`'auto'`, powerMode `'auto'`, ha mode `'rest'`, one page).

`powerMode` exists because supply detection is inferred from VBAT alone and can be wrong —
a full resting cell sits above the USB threshold (FR-8). The UI must expose it as a
three-way control with `'auto'` labelled as inferred, and the round-trip test must include
a non-default value so the field cannot be silently dropped.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd webapp && npm test`
Expected: 4 tests PASS. Also run `npx tsc --noEmit` → clean.

- [ ] **Step 5: Commit**

```bash
git add webapp/package.json webapp/tsconfig.json webapp/vitest.config.ts \
        webapp/vite.config.ts webapp/index.html webapp/src/model webapp/test
git commit -m "feat(webapp): scaffold and versioned typed config model"
```

---

### Task 17: 1-bit renderer in the web app, matching the firmware (NFR-4)

The web app's renderer must produce **the same bits** as the firmware's, or the preview
lies (NFR-4).

**Files:**
- Create: `webapp/src/canvas/bitmap.ts`
- Create: `webapp/src/canvas/render.ts`
- Create: `webapp/test/render.test.ts`
- Create: `webapp/test/fixtures/golden-default.json`

**Interfaces:**
- Produces: `createBitmap(): Bitmap` (a `Uint8Array(FB_BYTES)` wrapper with
  `setPx(x,y,black)` / `getPx(x,y)`), `blitMasked(dst, src, x, y, w, h)`,
  `renderPage(page, values): Uint8Array`.
- **Consumes the firmware's golden fixture**: the same `default_layout.bin` produced in
  Task 13 Step 5. The web app test compares its output to that file byte-for-byte.

- [ ] **Step 1: Write the failing tests**

`webapp/test/render.test.ts`:
```ts
import { describe, it, expect } from 'vitest';
import { readFileSync, existsSync } from 'node:fs';
import { createBitmap, FB_BYTES } from '../src/canvas/bitmap';
import { renderPage } from '../src/canvas/render';
import type { Page } from '../src/model/config';

describe('1-bit bitmap', () => {
  it('has the exact panel size', () => {
    expect(createBitmap().data.length).toBe(FB_BYTES);
  });

  it('writes MSB-first, matching the firmware (HW-6)', () => {
    const b = createBitmap();
    b.setPx(0, 0, true);      // black => clear the MSB of byte 0
    expect(b.data[0]).toBe(0x7f);
    expect(b.getPx(0, 0)).toBe(true);
    expect(b.getPx(1, 0)).toBe(false);
  });

  it('starts all-white', () => {
    expect(createBitmap().data.every((v) => v === 0xff)).toBe(true);
  });
});

describe('renderer', () => {
  it('matches the firmware golden image byte-for-byte (NFR-4, NFR-9)', () => {
    const goldenPath = 'test/fixtures/golden-default.bin';
    if (!existsSync(goldenPath)) return;   // generated in Task 13 Step 5
    const golden = new Uint8Array(readFileSync(goldenPath));
    const page = JSON.parse(
      readFileSync('test/fixtures/golden-default.json', 'utf8'),
    ) as Page;
    const out = renderPage(page, { 'golden': '68.4' });
    expect(out.length).toBe(FB_BYTES);
    expect(Buffer.from(out).equals(Buffer.from(golden))).toBe(true);
  });
});
```

- [ ] **Step 2: Run to verify failure**

Run: `cd webapp && npm test`
Expected: FAIL — `../src/canvas/bitmap` not found.

- [ ] **Step 3: Implement `bitmap.ts` and `render.ts`**

`createBitmap` allocates `FB_BYTES` filled with `0xFF` (white) and implements the **same
bit convention as the firmware canvas**: 1 = white, 0 = black, MSB-first, row-major,
pitch 115. `renderPage` walks the page's widgets in order, blitting static content and
then drawing dynamic values from `values` — mirroring `render_compose` in Task 13.

- [ ] **Step 4: Run to verify pass, and cross-check against the firmware**

Run: `cd webapp && npm test`
Expected: bitmap tests PASS; the golden comparison PASSES once the fixture is copied from
`firmware/tools/golden/`. **If it does not match, one of the two renderers is wrong — fix
the code, never the fixture.**

- [ ] **Step 5: Commit**

```bash
git add webapp/src/canvas webapp/test/render.test.ts webapp/test/fixtures
git commit -m "feat(webapp): 1-bit bitmap and renderer verified against the firmware golden image"
```

---

### Task 18: Canvas editor — drag, resize, snap (FR-20, FR-21)

**Files:**
- Create: `webapp/src/canvas/editor.ts`
- Create: `webapp/src/canvas/geometry.ts`
- Create: `webapp/test/geometry.test.ts`
- Create: `webapp/test/editor.test.ts`

**Interfaces:**
- Produces (pure, fully testable):
  - `hitTest(widgets, x, y): { id, zone } | null` where `zone` is
    `'move' | 'n' | 's' | 'e' | 'w' | 'ne' | 'nw' | 'se' | 'sw'`
  - `applyDrag(widget, start, dx, dy, opts): Widget`
  - `applyResize(widget, zone, start, dx, dy, opts): Widget`
  - `snap(value, grid, guides): number`
  - `clampToPanel(rect): Rect` — a widget can never leave the 920×680 canvas
- Produces (DOM): `attachEditor(canvasEl, state, onChange)`.

- [ ] **Step 1: Write the failing tests (pure geometry only)**

`webapp/test/geometry.test.ts`:
```ts
import { describe, it, expect } from 'vitest';
import { hitTest, applyResize, clampToPanel, snap } from '../src/canvas/geometry';
import type { Widget } from '../src/model/config';

const w = (over: Partial<Widget> = {}): Widget => ({
  id: 'a', x: 100, y: 100, w: 200, h: 100, role: 'dynamic', ...over,
});

describe('hit testing', () => {
  it('finds the widget under the cursor', () => {
    expect(hitTest([w()], 200, 150)?.id).toBe('a');
    expect(hitTest([w()], 50, 50)).toBeNull();
  });

  it('reports the resize zone at an edge and a corner', () => {
    expect(hitTest([w()], 100, 150)?.zone).toBe('w');
    expect(hitTest([w()], 100, 100)?.zone).toBe('nw');
    expect(hitTest([w()], 200, 150)?.zone).toBe('move');
  });

  it('prefers the topmost widget when they overlap', () => {
    const a = w({ id: 'a' });
    const b = w({ id: 'b', x: 150, y: 120 });
    expect(hitTest([a, b], 200, 150)?.id).toBe('b');
  });
});

describe('resize', () => {
  it('resizing the east edge changes width, not x', () => {
    const r = applyResize(w(), 'e', { x: 300, y: 0 }, 50, 0, { grid: 1, minW: 20, minH: 20 });
    expect(r.x).toBe(100);
    expect(r.w).toBe(250);
  });

  it('resizing the west edge moves x and shrinks width together', () => {
    const r = applyResize(w(), 'w', { x: 0, y: 0 }, 20, 0, { grid: 1, minW: 20, minH: 20 });
    expect(r.x).toBe(120);
    expect(r.w).toBe(180);
  });

  it('never resizes below the minimum', () => {
    const r = applyResize(w(), 'e', { x: 0, y: 0 }, -1000, 0, { grid: 1, minW: 20, minH: 20 });
    expect(r.w).toBe(20);
  });
});

describe('clamping and snapping', () => {
  it('pulls a widget fully back inside the panel', () => {
    const r = clampToPanel({ x: 900, y: 660, w: 200, h: 100 });
    expect(r.x + r.w).toBeLessThanOrEqual(920);
    expect(r.y + r.h).toBeLessThanOrEqual(680);
  });

  it('snaps to the nearest grid multiple', () => {
    expect(snap(103, 8, [])).toBe(104);
    expect(snap(99, 8, [])).toBe(96);
  });

  it('prefers a nearby alignment guide over the grid', () => {
    expect(snap(101, 8, [100])).toBe(100);
  });
});
```

- [ ] **Step 2: Run to verify failure**

Run: `cd webapp && npm test`
Expected: FAIL — geometry module not found.

- [ ] **Step 3: Implement `geometry.ts`**

`webapp/src/canvas/geometry.ts`:
```ts
import type { Widget } from '../model/config';
import { PANEL_WIDTH, PANEL_HEIGHT } from '../model/canvas-consts';

const EDGE = 8;   /* grab zone for edges/corners, in panel pixels */

export type Zone = 'move' | 'n' | 's' | 'e' | 'w' | 'ne' | 'nw' | 'se' | 'sw';
export interface Rect { x: number; y: number; w: number; h: number }
export interface ResizeOpts { grid: number; minW: number; minH: number }

export function hitTest(widgets: Widget[], x: number, y: number):
    { id: string; zone: Zone } | null {
  for (let i = widgets.length - 1; i >= 0; i--) {   /* topmost first */
    const wd = widgets[i]!;
    if (!(x >= wd.x && x <= wd.x + wd.w && y >= wd.y && y <= wd.y + wd.h)) continue;
    const left = x <= wd.x + EDGE;
    const right = x >= wd.x + wd.w - EDGE;
    const top = y <= wd.y + EDGE;
    const bottom = y >= wd.y + wd.h - EDGE;
    if (top && left) return { id: wd.id, zone: 'nw' };
    if (top && right) return { id: wd.id, zone: 'ne' };
    if (bottom && left) return { id: wd.id, zone: 'sw' };
    if (bottom && right) return { id: wd.id, zone: 'se' };
    if (left) return { id: wd.id, zone: 'w' };
    if (right) return { id: wd.id, zone: 'e' };
    if (top) return { id: wd.id, zone: 'n' };
    if (bottom) return { id: wd.id, zone: 's' };
    return { id: wd.id, zone: 'move' };
  }
  return null;
}

export function applyResize(wd: Widget, zone: Zone, _start: { x: number; y: number },
                            dx: number, dy: number, o: ResizeOpts): Widget {
  let { x, y, w, h } = wd;
  if (zone.includes('e')) w = Math.max(o.minW, wd.w + dx);
  if (zone.includes('s')) h = Math.max(o.minH, wd.h + dy);
  /* West/north resizes move the origin AND the size together, so the opposite
   * edge stays pinned. */
  if (zone.includes('w')) {
    const nw = Math.max(o.minW, wd.w - dx);
    x = wd.x + (wd.w - nw);
    w = nw;
  }
  if (zone.includes('n')) {
    const nh = Math.max(o.minH, wd.h - dy);
    y = wd.y + (wd.h - nh);
    h = nh;
  }
  return { ...wd, x, y, w, h };
}

/* Clamp position BEFORE size, so an oversized widget stays fully inside the
 * panel rather than overflowing. */
export function clampToPanel(r: Rect): Rect {
  let { x, y, w, h } = r;
  if (w > PANEL_WIDTH) w = PANEL_WIDTH;
  if (h > PANEL_HEIGHT) h = PANEL_HEIGHT;
  x = Math.min(Math.max(0, x), PANEL_WIDTH - w);
  y = Math.min(Math.max(0, y), PANEL_HEIGHT - h);
  return { x, y, w, h };
}

/* Guides win over the grid when within tolerance; otherwise snap to the grid. */
export function snap(value: number, grid: number, guides: number[]): number {
  const tol = Math.max(2, grid / 2);
  let best = value;
  let bestD = tol + 1;
  for (const g of guides) {
    const d = Math.abs(value - g);
    if (d < bestD) { bestD = d; best = g; }
  }
  if (bestD <= tol) return best;
  return Math.round(value / grid) * grid;
}
```
> **Verified:** this exact implementation passes all 9 geometry tests, including the
> west-edge `x`+`w` coupling and the guide-over-grid snap preference.

- [ ] **Step 4: Run to verify pass**

Run: `cd webapp && npm test`
Expected: all geometry tests PASS.

- [ ] **Step 5: Implement `editor.ts` (DOM layer) and wire it up**

`attachEditor` draws the current page to a `<canvas>` scaled to the panel aspect, converts
pointer events to panel coordinates, and dispatches to the pure functions above. Drawing
uses `renderPage` so **what the editor shows is the real 1-bit output**.

- [ ] **Step 6: Manual verification in a browser**

Run: `cd webapp && npm run dev` and, in the browser: drag a widget (it snaps), resize from
each edge and corner (clamped at the panel edge and the minimum size), and confirm the
rendered preview stays crisp 1-bit with no anti-aliasing.

- [ ] **Step 7: Commit**

```bash
git add webapp/src/canvas/geometry.ts webapp/src/canvas/editor.ts webapp/test/geometry.test.ts
git commit -m "feat(webapp): canvas editor with drag, resize, snapping and panel clamping"
```

---

### Task 19: Data binding, alert editor, and the entity picker (FR-23, FR-24, FR-14)

**Files:**
- Create: `webapp/src/data/owm.ts`
- Create: `webapp/src/data/ha.ts`
- Create: `webapp/src/alerts/rules.ts`
- Create: `webapp/src/ui/property-panel.ts`
- Create: `webapp/src/ui/map-picker.ts`
- Create: `webapp/test/alerts.test.ts`
- Create: `webapp/test/binding.test.ts`

**Interfaces:**
- Produces:
  - `formatValue(v: {value:number; status:string}, f?: Format): string`
  - `describeBinding(b: DataBinding): string` — a human label for the property panel
  - `HA_CLIENT.listEntities()` / `HA_CLIENT.validateEntity(id)` — **must** use
    `GET /api/states` (one call, in the browser, where memory is not constrained) and
    report a clear error for an unknown entity (the device path uses the template call).
  - `evaluateAlerts(rules: AlertRule[], value: number): AlertLevel` — **mirrors the
    firmware's `alerts_eval_all` semantics exactly**, including "non-finite never alarms"
- Task 18's editor and Task 20's save/load both use these.

- [ ] **Step 1: Write the failing tests — mirroring the firmware rules**

`webapp/test/alerts.test.ts`:
```ts
import { describe, it, expect } from 'vitest';
import { evaluateAlerts } from '../src/alerts/rules';

describe('alert evaluation (mirrors the firmware, FR-14)', () => {
  it('is strict for gt and inclusive for gte', () => {
    expect(evaluateAlerts([{ op: 'gt', threshold: 100, level: 'severe' }], 100.1)).toBe('severe');
    expect(evaluateAlerts([{ op: 'gt', threshold: 100, level: 'severe' }], 100)).toBe('none');
    expect(evaluateAlerts([{ op: 'gte', threshold: 100, level: 'severe' }], 100)).toBe('severe');
  });

  it('returns the most severe level that fires', () => {
    const rules = [
      { op: 'gt' as const, threshold: 90, level: 'advisory' as const },
      { op: 'gt' as const, threshold: 100, level: 'severe' as const },
    ];
    expect(evaluateAlerts(rules, 105)).toBe('severe');
    expect(evaluateAlerts(rules, 95)).toBe('advisory');
    expect(evaluateAlerts(rules, 50)).toBe('none');
  });

  it('never alarms on a non-finite value (an unavailable sensor)', () => {
    expect(evaluateAlerts([{ op: 'lt', threshold: 0, level: 'severe' }], NaN)).toBe('none');
    expect(evaluateAlerts([{ op: 'lt', threshold: 0, level: 'severe' }], Infinity)).toBe('none');
  });
});
```

`webapp/test/binding.test.ts`:
```ts
import { describe, it, expect } from 'vitest';
import { formatValue } from '../src/data/owm';
import type { DataBinding } from '../src/model/config';

describe('value formatting', () => {
  it('applies decimals, prefix and suffix', () => {
    expect(formatValue({ value: 68.44, status: 'ok' }, { decimals: 1, suffix: '°F' }))
      .toBe('68.4°F');
    expect(formatValue({ value: 68, status: 'ok' }, { decimals: 0, suffix: '°F' }))
      .toBe('68°F');
  });

  it('shows the fallback for an unavailable value, never a zero or NaN', () => {
    const f = { fallback: '--' };
    expect(formatValue({ value: 0, status: 'unavailable' }, f)).toBe('--');
    expect(formatValue({ value: NaN, status: 'ok' }, f)).toBe('--');
  });
});
```

- [ ] **Step 2: Run to verify failure**

Run: `cd webapp && npm test`
Expected: FAIL — modules not found.

- [ ] **Step 3: Implement**

- `alerts/rules.ts`: implement `evaluateAlerts` with the **same comparison semantics and
  severity ordering** as `lib/alerts/src/alerts.c`. Non-finite input returns `'none'`.
- `data/owm.ts`: `formatValue` applying decimals/prefix/suffix, and returning
  `fallback` (or `'--'`) whenever the status is not `ok` or the value is not finite.
- `data/ha.ts`: an HA client for the **browser only** — `listEntities()` calls
  `GET /api/states` once (fine here, unlike on-device) and `validateEntity(id)` reports
  whether the entity exists, so the UI can reject a typo immediately.
- `ui/property-panel.ts`: geometry, binding picker, format fields, font, and the alert-rule
  list; `ui/map-picker.ts`: Leaflet (or a canvas map) writing `latitude`/`longitude` into
  the config, plus the zip field.

- [ ] **Step 4: Run to verify pass**

Run: `cd webapp && npm test`
Expected: alert and binding tests PASS.

- [ ] **Step 5: Manual verification**

In the browser: bind a widget to `sensor.upstairs_hallway_temperature`, confirm the entity
picker validates it; add a rule `temp > 100 → severe` and confirm the preview shows the
alert bar when you set a test value of 105.

- [ ] **Step 6: Commit**

```bash
git add webapp/src/data webapp/src/alerts webapp/src/ui webapp/test/alerts.test.ts \
        webapp/test/binding.test.ts
git commit -m "feat(webapp): binding, formatting, alert editor and entity validation"
```

---

### Task 20: Save/load configs + push to device (FR-26, FR-27, IF-2a)

**Files:**
- Create: `webapp/src/transfer/config.ts`
- Create: `webapp/src/transfer/bitmap.ts`
- Create: `webapp/src/transfer/device.ts`
- Create: `webapp/test/config-io.test.ts`
- Create: `webapp/test/bitmap-chunks.test.ts`

**Interfaces:**
- Produces:
  - `exportConfig(c: Config): string` / `importConfig(text: string): Config` —
    **version-checked**: a newer `schemaVersion` is refused with a clear message (FR-26b)
  - `chunkBitmap(data: Uint8Array, size: number): Uint8Array[]` — 78,200 bytes split for
    streaming (IF-2a), last chunk possibly short
  - `pushConfig(deviceUrl, c)` / `pushBitmap(deviceUrl, data, onProgress)`
- The file save/load uses **download/upload JSON** (FR-26a) — the File System Access API
  is optional progressive enhancement only.

- [ ] **Step 1: Write the failing tests**

`webapp/test/config-io.test.ts`:
```ts
import { describe, it, expect } from 'vitest';
import { exportConfig, importConfig } from '../src/transfer/config';
import { emptyConfig } from '../src/model/config';

describe('config file I/O', () => {
  it('round-trips a config through a saved file', () => {
    const c = emptyConfig();
    expect(importConfig(exportConfig(c))).toEqual(c);
  });

  it('embeds schemaVersion and a timestamp so a file is self-describing (FR-26b)', () => {
    const text = exportConfig(emptyConfig(new Date('2026-09-18T12:00:00Z')));
    const parsed = JSON.parse(text);
    expect(parsed.schemaVersion).toBe(1);
    expect(parsed.createdAt).toBe('2026-09-18T12:00:00.000Z');
    expect(parsed.generator).toBe('eink-weather-webapp');
  });

  it('refuses a file from a newer version instead of mis-reading it', () => {
    expect(() => importConfig('{"schemaVersion": 99, "pages": []}')).toThrow(/newer/i);
  });

  it('refuses malformed input', () => {
    expect(() => importConfig('not json')).toThrow();
  });
});
```

`webapp/test/bitmap-chunks.test.ts`:
```ts
import { describe, it, expect } from 'vitest';
import { chunkBitmap } from '../src/transfer/bitmap';
import { FB_BYTES } from '../src/model/canvas-consts';

describe('bitmap chunking (IF-2a)', () => {
  it('splits the exact framebuffer size into chunks that reassemble losslessly', () => {
    const data = new Uint8Array(FB_BYTES);
    for (let i = 0; i < data.length; i++) data[i] = i & 0xff;
    const chunks = chunkBitmap(data, 4096);
    expect(chunks.reduce((n, c) => n + c.length, 0)).toBe(FB_BYTES);
    const joined = new Uint8Array(FB_BYTES);
    let off = 0;
    for (const c of chunks) { joined.set(c, off); off += c.length; }
    expect(joined).toEqual(data);
  });

  it('produces a short final chunk when it does not divide evenly', () => {
    const data = new Uint8Array(FB_BYTES);
    const chunks = chunkBitmap(data, 4096);
    expect(chunks.length).toBe(Math.ceil(FB_BYTES / 4096));
    expect(chunks[chunks.length - 1]!.length).toBeLessThanOrEqual(4096);
  });
});
```
> **Note:** `FB_BYTES` is imported from `../src/model/canvas-consts` (created in Task 16)
> rather than from `config.ts`, so this node test does not pull in DOM globals.

- [ ] **Step 2: Run to verify failure**

Run: `cd webapp && npm test`
Expected: FAIL — modules not found.

- [ ] **Step 3: Implement**

- `transfer/config.ts`: JSON serialise/parse with the version gate and a descriptive error.
- `transfer/bitmap.ts`: `chunkBitmap` plus the upload driver that sends chunks with
  `offset`/`total` and surfaces progress; on any failure it must not leave the device
  thinking a new bitmap is active (the device enforces this too, in Task 14).
- `transfer/device.ts`: `fetch` wrappers for `/api/status`, `/api/config` (GET/PUT),
  `/api/bitmap`, `/api/refresh`; surface HTTP errors with the device's message.

- [ ] **Step 4: Run to verify pass**

Run: `cd webapp && npm test`
Expected: config-io and bitmap-chunk tests PASS.

- [ ] **Step 5: On-hardware verification**

With a device on the LAN, use the app to push a config and a bitmap, then confirm the
panel shows the new layout. Then **interrupt** an upload (kill the tab midway) and confirm
`GET /api/status` still reports the previous bitmap as active.

- [ ] **Step 6: Commit**

```bash
git add webapp/src/transfer webapp/src/model webapp/test/config-io.test.ts \
        webapp/test/bitmap-chunks.test.ts
git commit -m "feat(webapp): config save/load, chunked bitmap upload, device client"
```

---

### Task 21: Default layout for the stated use case (FR-17)

**Files:**
- Create: `webapp/src/presets/default-layout.ts`
- Create: `webapp/test/default-layout.test.ts`
- Create: `webapp/test/fixtures/golden-default.json` (generated from this preset)

**Interfaces:**
- Produces: `defaultLayout(): Config` — the shipped starting point implementing the user's
  stated intent, and the source of the golden fixture used by Tasks 13 and 17.

- [ ] **Step 1: Write the failing test**

`webapp/test/default-layout.test.ts`:
```ts
import { describe, it, expect } from 'vitest';
import { defaultLayout } from '../src/presets/default-layout';
import { PANEL_WIDTH, PANEL_HEIGHT } from '../src/model/canvas-consts';

describe('default layout (FR-17)', () => {
  const c = defaultLayout();

  it('binds exactly the entities the user asked for', () => {
    const ids = c.pages.flatMap((p) => p.widgets)
      .map((w) => w.binding?.entityId)
      .filter(Boolean);
    expect(ids).toContain('sensor.upstairs_hallway_temperature');
    expect(ids).toContain('sensor.64b708cfe0fc_sensor_2_temperature_f');
  });

  it('binds OpenWeatherMap current conditions and forecast', () => {
    const kinds = c.pages.flatMap((p) => p.widgets).map((w) => w.binding?.kind);
    expect(kinds).toContain('owm-current');
    expect(kinds).toContain('owm-daily');
  });

  it('every widget lies inside the panel', () => {
    for (const p of c.pages) {
      for (const w of p.widgets) {
        expect(w.x).toBeGreaterThanOrEqual(0);
        expect(w.y).toBeGreaterThanOrEqual(0);
        expect(w.x + w.w).toBeLessThanOrEqual(PANEL_WIDTH);
        expect(w.y + w.h).toBeLessThanOrEqual(PANEL_HEIGHT);
      }
    }
  });

  it('no two widgets overlap (a clean starting point, not a pile)', () => {
    for (const p of c.pages) {
      for (let i = 0; i < p.widgets.length; i++) {
        for (let j = i + 1; j < p.widgets.length; j++) {
          const a = p.widgets[i]!, b = p.widgets[j]!;
          const overlap = a.x < b.x + b.w && b.x < a.x + a.w &&
                          a.y < b.y + b.h && b.y < a.y + a.h;
          expect(overlap, `${a.id} overlaps ${b.id}`).toBe(false);
        }
      }
    }
  });

  it('ships alert rules for extreme weather (FR-14)', () => {
    const all = c.pages.flatMap((p) => p.widgets);
    expect(all.some((w) => (w.alerts?.length ?? 0) > 0)).toBe(true);
    expect(all.some((w) => w.showsOwmAlerts)).toBe(true);
  });
});
```

- [ ] **Step 2: Run to verify failure**

Run: `cd webapp && npm test`
Expected: FAIL — preset not found.

- [ ] **Step 3: Implement the default layout**

Design a **landscape 920×680** dashboard (this is a real design task, not boilerplate):
a large current-temperature reading, the two Home Assistant temps with labels, a weather
icon + condition, a short multi-day forecast strip, a location/zip line, and an alert bar.
Give sensible rules (e.g. temp > 100 °F → severe, < 20 °F → severe, wind > 25 mph →
advisory). Add a second page for the forecast so paging is exercised by default.

- [ ] **Step 4: Run to verify pass**

Run: `cd webapp && npm test`
Expected: all default-layout tests PASS, and the overlap test forces genuinely clean
geometry.

- [ ] **Step 5: Generate the golden fixture and re-run the cross-check**

Emit `webapp/test/fixtures/golden-default.json` from this preset, then regenerate
`firmware/tools/golden/default_layout.bin` with the same values so Task 13 and Task 17
compare against a real layout. Re-run both suites:
Expected: web app and firmware golden tests both PASS against the same fixture.

- [ ] **Step 6: Commit**

```bash
git add webapp/src/presets webapp/test/default-layout.test.ts webapp/test/fixtures \
        firmware/tools/golden
git commit -m "feat(webapp): default weather dashboard layout with alert rules (FR-17)"
```

---

### Task 21b: Serve the web app from device flash (FR-18, FR-19, FR-19a)

The user chose **both** delivery modes: `npm run dev` locally *and* the built app embedded
in the ESP32 so the device serves its own UI with no install.

**Files:**
- Create: `webapp/vite.config.ts` (modify: single-file / inlined-asset build target)
- Create: `webapp/scripts/build-for-device.mjs`
- Create: `firmware/components/webui/CMakeLists.txt`
- Create: `firmware/components/webui/webui.c`
- Modify: `firmware/components/api/api_config.c` (mount the static handler at `/`)

**Interfaces:**
- Produces: `esp_err_t webui_mount(httpd_handle_t server)` — registers a catch-all GET
  handler that serves the embedded assets, with `index.html` for `/`.
- The build script produces `firmware/components/webui/www/` containing the app, embedded
  via `EMBED_FILES` so it lives in flash, **not** RAM.

- [ ] **Step 1: Configure a device-friendly build**

The whole bundle must be small enough for the ~896 KiB `storage`/app headroom. Add a Vite
build mode with minification, **inlined** small assets to minimise request count, and gzip
pre-compression of the output files. Add a size check that **fails the build** if the
gzipped bundle exceeds a documented budget (start at 300 KiB; tighten once measured), so
the app cannot silently outgrow the device.

- [ ] **Step 2: Write the build script**

`webapp/scripts/build-for-device.mjs` runs the Vite build and copies the output into
`firmware/components/webui/www/`, plus a generated manifest of path → (embedded symbol,
content-type, gzipped?). It must be safe to re-run and must clean stale files.

- [ ] **Step 3: Embed and serve from the device**

`firmware/components/webui/CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS "webui.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server
    EMBED_FILES ${WEBUI_FILES}     ; populated from www/, gzip variants included
)
```

`webui.c` registers a catch-all handler that:
- serves the correct `Content-Type`;
- sends `Content-Encoding: gzip` for the `.gz` variants and sets `Cache-Control`
  with a hash-based ETag so the browser caches assets across reloads;
- returns `index.html` for unknown paths (SPA routing);
- **never** shadows `/api/*` — register it after, or exclude that prefix explicitly.

- [ ] **Step 4: Verify the size budget**

Run: `cd webapp && npm run build:device`
Expected: build succeeds and prints the gzipped total; assert it is under the budget.

- [ ] **Step 5: On-hardware verification**

Flash and browse to `http://<device-ip>/` from another machine on the LAN:
- the full editor loads and is usable;
- **the same API client works against the device** (FR-19) — load the config, edit, push,
  and see the panel update;
- reload the page and confirm assets come from cache (ETag), and that `/api/status` still
  works (the UI did not shadow the API).

- [ ] **Step 6: Commit**

```bash
git add webapp/vite.config.ts webapp/scripts firmware/components/webui \
        firmware/components/api
git commit -m "feat(webui): embed and serve the built web app from device flash"
```

---

## Phase 6 — Polish (AFTER bugs are squashed)

> **Sequencing requirement (user's explicit instruction, 2026-09-18):** this phase runs
> **only after** Phases 0-5 pass their tests and the on-hardware checks. The user asked
> for a functional, clean app first with **product-grade polish afterwards**, so polish
> never masks a correctness bug and never blocks a fix.

### Task 22: Web app visual polish

**Files:**
- Modify: `webapp/index.html`, `webapp/src/main.ts`, `webapp/src/ui/*`
- Create: `webapp/src/ui/theme.css`

- [ ] **Step 1: Establish a visual system**

Define a small design system (spacing scale, one accent colour, type scale, focus states)
in `theme.css` and apply it. Use a real font stack; keep the app legible and calm.

- [ ] **Step 2: Polish the editor chrome**

Refine the canvas frame, property panel grouping, page tabs, selection handles, rulers/
grid, and drag affordances. Add clear empty states ("no widgets yet — drag from the
palette") and a visible save/push state machine (idle → uploading → ok/error).

- [ ] **Step 3: Make the preview honest and beautiful**

Add zoom controls and a 1-bit/4-grey toggle so the user can see exactly what the panel
will do. Ensure the **default state of the app shows a nice, complete dashboard**, not a
blank canvas.

- [ ] **Step 4: Verify no regressions**

Run: `cd webapp && npm test && npx tsc --noEmit`
Expected: all tests still PASS (polish must not change behaviour), typecheck clean.

- [ ] **Step 5: Commit**

```bash
git add webapp/src/ui webapp/src/main.ts webapp/index.html
git commit -m "style(webapp): visual polish for the layout editor and preview"
```

---

## Definition of done (checked against the spec, not asserted)

A phase is complete only when **all** of the following hold:

1. Every task's tests pass: `cd firmware && pio test -e native` (all suites) and
   `cd webapp && npm test`.
2. The firmware builds and fits: `cd firmware && pio run -e esp32dev` → `[SUCCESS]` with
   documented free RAM/flash headroom (NFR-2).
3. The golden-image test passes in **both** the firmware and the web app against the same
   fixture (NFR-4, NFR-9). **A vacuous pass does not count:** both tests are written to
   *skip* when the fixture is absent (`TEST_IGNORE_MESSAGE` / `if (!existsSync) return`),
   which shows up as green while comparing nothing. Before claiming item 3, confirm the
   fixture **exists** and that the test is executing its comparison:
   - `firmware/tools/golden/default_layout.bin` exists and is exactly **78,200 bytes**;
   - the webapp fixture was copied from it and is byte-identical
     (`cmp` the two files);
   - the firmware run reports the golden test as **run**, not ignored/skipped;
   - the webapp run does not take the early-return branch (temporarily corrupt one byte
     of a copy and confirm the test goes **red** — if it stays green, the comparison is
     not wired up).
   Until the fixture is generated and byte-identical on both sides, item 3 is **not met**.
4. Each Phase 4 task's **on-hardware check** was actually performed and observed, with the
   result noted in the commit message (NFR-7).
5. The e-ink lifetime rules hold: no more than N partials without a full refresh, and a
   full refresh at least every 24 h (FR-10, FR-11).
6. Nothing in the repo contains a credential; provisioning writes secrets to NVS only
   (FR-30).
7. **No test may pass vacuously.** A test that silently returns early, skips, or ignores
   itself when its input is missing is treated as **failing**, not passing. Every suite
   must additionally be shown to go **red** when the code under test is broken — a suite
   that has never been observed failing is not evidence that anything works. The golden
   renderer test is the highest-risk case (two independent implementations in two
   languages are being asserted equal), so it carries an explicit mutation check in item 3.

**No requirement is "done" because the code exists, compiles, or looks right.** It is done
when its test fails without it and passes with it, and — for anything touching hardware —
when the behaviour was seen on the device.

