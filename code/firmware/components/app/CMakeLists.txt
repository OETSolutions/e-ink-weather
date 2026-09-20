# `shared` compiles lib/ (see components/shared/CMakeLists.txt). `api` and `epd` are
# ordinary IDF components in this project.
idf_component_register(
    SRCS "app_boot.c" "app_refresh.c"
    INCLUDE_DIRS "include"
    REQUIRES shared api epd net prov esp_adc esp_timer nvs_flash
)
