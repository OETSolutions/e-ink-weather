# Compiles the shared logic under lib/ into the firmware as ONE IDF component.
#
# WHY THIS FILE HAS TO EXIST: PlatformIO's ESP-IDF builder does not build `lib/` at all.
# It builds only the components in CMake's code model, and a PlatformIO library is not one.
# The symptom is not a compile error — it is a LINK error naming every shared function
# (canvas_init, render_compose_stream, cfg_store_get, power_classify, ...), because the
# component that called them was built but the library holding them never was.
#
# WHY ONE COMPONENT AND NOT ONE PER LIBRARY: IDF links everything into a single firmware, so
# per-library granularity buys nothing here, and a single component avoids N CMakeLists.txt
# files to keep in sync. `lib/` keeps its library.json manifests, which is what the native
# test env uses — so the SAME tree still serves both builders (NFR-6).
#
# The sources are globbed rather than listed: a new file in lib/<name>/src/ is then picked
# up with no edit here, which is the behaviour the native env's LDF already gives.
file(GLOB_RECURSE SHARED_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../../lib/*/src/*.c"
)

# The include dirs are globbed for the same reason: adding a library under lib/ should need
# no edit here. Listing them by hand is what made the first build fail with
# "alerts.h: No such file or directory" — the source glob had picked up lib/alerts/src/
# while the include list had not been updated to match.
file(GLOB SHARED_INCLUDES
    "${CMAKE_CURRENT_LIST_DIR}/../../lib/*/include"
)

idf_component_register(
    SRCS ${SHARED_SOURCES}
    INCLUDE_DIRS ${SHARED_INCLUDES}
    # `json` is IDF's bundled cJSON (components/json/cJSON/cJSON.h) — the same source the
    # native env pulls from the PlatformIO registry. Using IDF's copy here keeps one cJSON
    # per firmware instead of two.
    REQUIRES json
)
