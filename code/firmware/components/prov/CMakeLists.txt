# Provisioning (FR-30): BLE via wifi_prov_mgr, plus our own SoftAP captive portal.
#
# WHY FOUR FILES AND NOT ONE: the transports and the storage have genuinely different
# concerns, and keeping them apart is what makes the shared exit auditable — every write of
# a credential happens in prov_store.c, whichever transport produced it. See prov.h for why
# the manager drives BLE and cannot also drive the AP.
#
# `bt` is a PRIV_REQUIRES: the BLE transport lives in it, but nothing outside this component
# includes a Bluetooth header — the manager owns the whole BLE session. Keeping it private
# means the rest of the firmware cannot accidentally depend on BT being initialised.
#
# `esp_http_server` is needed for the config page, and `lwip` for the DNS catch-all's
# sockets and the AP address lookup.
#
# `shared` is what puts lib/devcfg/include/nvs_keys.h on the include path. It is the
# component that exposes every lib/ header, and it must be a REQUIRES rather than a
# PRIV_REQUIRES for exactly the same reason as everything else here: a component's own
# include dirs are private, so without this the keys header is not visible and the build
# fails with "nvs_keys.h: No such file or directory" — which reads like a missing file
# rather than a missing dependency.
#
# `provdns` holds the DNS reply construction. It is a separate library, not code in this
# component, because it is pure wire-format logic with two fixed bugs behind it and it needs
# host tests — see lib/provdns/include/provdns.h. It needs no entry in REQUIRES: `shared`
# globs lib/*/src and lib/*/include, so the library is compiled into `shared` and its header
# arrives with it. Naming it here would fail with "Failed to resolve component 'provdns'" —
# it is a PlatformIO library, not an IDF component.
idf_component_register(
    SRCS
        "prov.c"         # orchestration: the manager session, the AP lifecycle, the exit
        "prov_ap.c"      # the setup AP, the config page and the captive-portal redirect
        "prov_dns.c"     # DNS catch-all that makes the page open by itself
        "prov_store.c"   # the one place credentials and config are written to NVS
        "prov_softap_prov.c" # second transport: HTTP protocomm for the SoftAP app
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi nvs_flash esp_netif esp_event wifi_provisioning protocomm esp_http_server json shared
    PRIV_REQUIRES bt lwip
)
