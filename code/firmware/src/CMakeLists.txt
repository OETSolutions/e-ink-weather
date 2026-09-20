# PlatformIO generates this file for projects without one. It compiles everything
# under src/. The vendor reference image lives in src/gimage1_data.c as a plain C
# array rather than an EMBED_FILES binary: PlatformIO's src/ handling rewrites the
# generated .S path and EMBED_FILES fails to resolve under it (verified).
FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

# REQUIRES `api` (and, through it, `epd`) explicitly.
#
# Without this IDF links the component OUT entirely: a component is only built if
# something requires it, and `src/` referenced neither. The build SUCCEEDED and produced a
# firmware.bin byte-identical to the one without the API — no error, no warning, and every
# endpoint simply missing at runtime. Anything meant to be reachable from app_main has to
# be named here.
idf_component_register(
    SRCS ${app_sources}
    REQUIRES api app epd net prov
)
