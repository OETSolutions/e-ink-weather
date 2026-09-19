# `shared` is the IDF component that compiles everything under lib/ (see
# components/shared/CMakeLists.txt for why that indirection exists). Requiring it here
# brings in the apifmt/devcfg/layout/upload headers and objects.
#
# `net` is required because its include dir must be on this component's path for
# net_wifi.h, and because the HTTP server needs net_stack_init() before it can listen —
# a component's own includes are private to it, so a REQUIRES is what grants the path.
idf_component_register(
    SRCS "api_server.c" "api_store.c" "api_ota.c"
    INCLUDE_DIRS "include"
    REQUIRES shared net esp_http_server esp_https_ota app_update nvs_flash esp_timer json mbedtls
)
