# NOTE: this component may NOT require `layout` — that is a PlatformIO library under lib/,
# not an IDF component, and IDF would fail with "Failed to resolve component 'layout'".
#
# `shared` is required (not private) for lib/devcfg/include/nvs_keys.h, the one definition of
# the NVS namespace and key names: geo_ip.c writes the location keys and must not spell them
# as string literals, which would silently drift from the readers in app_refresh.c and
# prov_store.c.
idf_component_register(
    SRCS "net_wifi.c" "net_http.c" "geo_ip.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif nvs_flash esp_http_client esp-tls mbedtls esp_timer shared
)
