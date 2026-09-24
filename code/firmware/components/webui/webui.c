/* Serve the web app out of flash (FR-18, FR-19).
 *
 * The app is embedded with EMBED_FILES, so the bytes live in the flash-mapped region and the
 * handler streams them straight out — nothing is copied into RAM. That matters here more than
 * usual: the device has no PSRAM, and holding a 64 KiB bundle resident would fight the two
 * framebuffers for the little DRAM there is.
 *
 * GZIP: every text asset is stored pre-compressed and marked with Content-Encoding, so 214 KB
 * of JavaScript goes over wifi as 64 KB. The device does no compression — the build script does
 * it — which keeps zlib off the hot path entirely.
 *
 * CACHING: assets are served with a hash-free, build-specific ETag and a long max-age. The
 * ETag changes when the bundle is rebuilt because it is derived from the asset's length and the
 * firmware's build identity, so a flashed update invalidates a browser's cached copy without
 * the user being told to hard-refresh. */

#include "webui.h"
#include "webui_assets.h"

#include "esp_log.h"
#include "esp_app_desc.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "webui";

/* The assets are a plain table of {url, data, len, type, gzipped}, generated from the built
 * app. No symbol mangling, no asm labels: the generator emits the arrays and the table together,
 * so they cannot drift, and a missing asset is a compile error. */

/** Does `path` look like a request for a FILE, or for an app ROUTE?
 *
 * A route (no extension) must serve index.html; a file that is genuinely missing must 404
 * rather than quietly returning HTML, because a browser that receives HTML where it expected
 * JavaScript fails with a syntax error that points at the wrong thing entirely. */
static int looks_like_a_file(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *last = slash ? slash + 1 : path;
    return strchr(last, '.') != NULL;
}

static const webui_asset_t *find_asset(const char *url)
{
    for (int i = 0; i < WEBUI_ASSET_COUNT; i++) {
        if (strcmp(WEBUI_ASSETS[i].url, url) == 0) return &WEBUI_ASSETS[i];
    }
    return NULL;
}

/* The ETag. Derived from the firmware's build identity plus the asset length, so it changes on
 * every reflash. A per-asset content hash would be finer-grained, but the whole bundle is
 * rebuilt together and is 70 KB — re-fetching all of it after an update is not worth the extra
 * table. */
static void make_etag(const webui_asset_t *a, const uint8_t *start, size_t len,
                      char *out, size_t out_len)
{
    const esp_app_desc_t *d = esp_app_get_description();
    (void)a;
    /* Hash the version STRING and the first 4 bytes of the ELF SHA-256. d->version is a char[32]
     * array, so casting it to an integer would fold the ADDRESS, not the value — it looked like a
     * build identity only because the address happens to move per build. The embedded SHA-256 is
     * the honest build identity the comment above promises. */
    uint32_t h = 2166136261u;
    for (const char *p = d ? d->version : "?"; *p; p++) {
        h = (h ^ (unsigned char)*p) * 16777619u;
    }
    if (d) {
        for (int i = 0; i < 4; i++) h = (h ^ d->app_elf_sha256[i]) * 16777619u;
    }
    snprintf(out, out_len, "\"%08lx-%u\"", (unsigned long)h, (unsigned)len);
}

static esp_err_t serve_asset(httpd_req_t *req, const webui_asset_t *a)
{
    const uint8_t *start = a->data;
    const size_t len = a->len;
    if (!start || len == 0) {
        ESP_LOGE(TAG, "asset %s has no data", a->url);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "asset missing");
    }

    char etag[32];
    make_etag(a, start, len, etag, sizeof(etag));
    httpd_resp_set_hdr(req, "ETag", etag);

    /* A CONDITIONAL REQUEST IS ANSWERED 304, which is the point of the ETag: a reload of the
     * editor then transfers a few hundred bytes instead of 70 KB. */
    char inm[64];
    if (httpd_req_get_hdr_value_str(req, "If-None-Match", inm, sizeof(inm)) == ESP_OK &&
        strcmp(inm, etag) == 0) {
        httpd_resp_set_status(req, "304 Not Modified");
        return httpd_resp_send(req, NULL, 0);
    }

    httpd_resp_set_type(req, a->content_type);
    if (a->gzipped) {
        /* Only correct because the stored bytes really ARE gzip. A browser that is told this
         * and receives plain bytes renders garbage, so the flag comes from the build script,
         * which only sets it when it actually compressed the file. */
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }
    /* EVERY ASSET REVALIDATES, because every asset changes at the SAME URL on a reflash.
     *
     * This used to be `public, max-age=86400` for anything but index.html, on the reasoning that
     * the bundle is 70 KB and re-fetching is wasteful. That reasoning is wrong for this device:
     * `max-age` tells the browser NOT TO ASK for a day, so after a reflash the user keeps running
     * the OLD bundle — a real symptom, not a theoretical one. The credentials UI was built,
     * flashed and verified on the device while the browser kept showing the previous UI with no
     * credentials section, because a 200 with a fresh ETag never reached it.
     *
     * `no-cache` does NOT mean "do not cache" — it means "revalidate before using". The ETag
     * above then makes the common case a 304 with a few hundred bytes, so the cost the old header
     * was avoiding is still avoided, and a changed bundle is picked up on the very next load. */
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    /* Sent in ONE call. httpd_resp_send reads directly from flash through the mmap, so there is
     * no intermediate buffer to size or overflow. */
    return httpd_resp_send(req, (const char *)start, (ssize_t)len);
}

static esp_err_t h_webui(httpd_req_t *req)
{
    /* NEVER SHADOW THE API. The handler is registered after the API routes, which should be
     * enough — but a future reordering would silently break the app in a way that looks like a
     * client bug, so this refuses the prefix outright. */
    if (strncmp(req->uri, "/api/", 5) == 0) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
    }

    /* Strip a query string: /?x=1 is the same document as /. */
    char url[192];
    size_t n = 0;
    for (const char *p = req->uri; *p && *p != '?' && n + 1 < sizeof(url); p++) url[n++] = *p;
    url[n] = '\0';

    if (n == 0 || strcmp(url, "/") == 0) {
        const webui_asset_t *idx = find_asset(WEBUI_INDEX_URL);
        if (!idx) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no index");
        return serve_asset(req, idx);
    }

    const webui_asset_t *a = find_asset(url);
    if (a) return serve_asset(req, a);

    /* Not an asset. A route (no extension) gets the app shell so client-side navigation works;
     * anything that looks like a file 404s honestly, rather than handing JavaScript requests an
     * HTML document. */
    if (!looks_like_a_file(url)) {
        const webui_asset_t *idx = find_asset(WEBUI_INDEX_URL);
        if (idx) return serve_asset(req, idx);
    }

    ESP_LOGW(TAG, "404 %s", url);
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
}

esp_err_t webui_mount(httpd_handle_t server)
{
    static const httpd_uri_t root = {
        /* A WILDCARD URI, not the bare root.
         *
         * esp_http_server's wildcard matcher treats a trailing star as a prefix wildcard, so
         * registering the bare "/" matches ONLY the exact path "/" — everything else falls
         * through to the server's own 404 BEFORE this handler is consulted. Measured: the root
         * served the index while every asset path returned httpd's own "Nothing matches the
         * given URI". The starred form is what catches the asset paths and the SPA routes with
         * one handler. */
        .uri = "/" "*",
        .method = HTTP_GET,
        .handler = h_webui,
    };
    const esp_err_t e = httpd_register_uri_handler(server, &root);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "could not register the web UI handler: %s", esp_err_to_name(e));
        return e;
    }
    /* The byte count is summed from the asset table rather than hardcoded. It used to print a
     * literal 0, which read as "the app is not actually embedded" in a boot log — the opposite of
     * the truth, and alarming for no reason. */
    unsigned total = 0;
    for (int i = 0; i < WEBUI_ASSET_COUNT; i++) total += (unsigned)WEBUI_ASSETS[i].len;
    ESP_LOGI(TAG, "web UI mounted (%d assets, %u bytes in flash)",
             WEBUI_ASSET_COUNT, total);
    return ESP_OK;
}
