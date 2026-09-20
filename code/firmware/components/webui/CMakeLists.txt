# Serve the built web app from device flash (FR-18, FR-19).
#
# THE ASSETS ARE A GENERATED C SOURCE, not EMBED_FILES.
#
# IDF supports EMBED_FILES and it would be the idiomatic choice, but PlatformIO's library
# dependency finder treats the `.S` stubs it generates as source files to compile and then
# resolves them at a DOUBLED path — "Source .pio/build/esp32dev/.pio/build/esp32dev/www_x.S not
# found" — and no arrangement of the option avoids it. Generating the byte arrays as one plain
# C file puts the mechanism under our control, removes the asm-label indirection, and makes a
# missing asset a COMPILE error rather than a link error naming a symbol nobody wrote.
#
# The arrays are `const`, so they live in flash (rodata) and are NOT copied into RAM.
#
# WHY THE OUTPUT IS COMMITTED (www/ is in git): this keeps `pio run` working with NO node
# toolchain, so a fresh clone can build and flash the firmware. A build step that shells out to
# npm would make the firmware unbuildable on any machine without the web app's dependencies, for
# a component most firmware work never touches. Rebuild with `npm run build:device` in webapp/.

idf_component_register(
    SRCS "webui.c"
         "www/webui_assets.c"
    INCLUDE_DIRS "include" "www"
    REQUIRES esp_http_server esp_app_format
)
