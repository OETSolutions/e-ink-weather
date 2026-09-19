# NOTE: `layout` is a PlatformIO library under lib/, NOT an IDF component, so it must not
# appear in REQUIRES — IDF would fail with "Failed to resolve component 'layout'".
# PlatformIO injects lib/ include dirs into src/ only, NOT into components/, so the
# include dir is added explicitly here. The library's objects are linked in by PlatformIO.
idf_component_register(
    SRCS "epd.c" "epd_spi.c" "epd_hw_spi.c" "gt30.c"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/lib/layout/include"
    REQUIRES driver esp_timer esp_driver_spi
)
