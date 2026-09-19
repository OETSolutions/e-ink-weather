# PlatformIO generates this file for projects without one. It compiles everything
# under src/. The vendor reference image lives in src/gimage1_data.c as a plain C
# array rather than an EMBED_FILES binary: PlatformIO's src/ handling rewrites the
# generated .S path and EMBED_FILES fails to resolve under it (verified).
FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(SRCS ${app_sources})
