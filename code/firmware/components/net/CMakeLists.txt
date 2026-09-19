# NOTE: this component may NOT require `layout` — that is a PlatformIO library under lib/,
# not an IDF component, and IDF would fail with "Failed to resolve component 'layout'".
idf_component_register(
    SRCS "net_wifi.c" "net_http.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif nvs_flash esp_http_client esp-tls mbedtls esp_timer
)
