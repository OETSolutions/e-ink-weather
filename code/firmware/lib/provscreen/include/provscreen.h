#pragma once

#include <stdint.h>

/* The provisioning screen: what the panel shows while the device is waiting to be set up.
 *
 * WHY THIS EXISTS. The device has a 920x680 display and, before this, showed the vendor logo
 * while provisioning — so the only way to find out how to configure it was to read the serial
 * log, with the phone that needs the URL already in hand. The screen is the natural place for
 * the instructions, and a QR code is the shortest path from "unconfigured" to "the setup page
 * is open on my phone": one camera app, no typing of an IP address.
 *
 * It draws into a caller-owned 1bpp framebuffer using the same canvas and font code as the
 * normal render path, so it obeys the one bit convention the whole firmware uses (bit clear =
 * black ink) and picks up the panel row-order fix in the driver for free. */

/* Render the instructions for the running AP.
 *
 * `ap_ssid` is the network to join and the BLE device name (they are the same string), and
 * `pop` is the BLE proof of possession, printed because this screen is only ever visible while
 * the device is deliberately unconfigured — see the security note in the implementation.
 *
 * Returns 0, or -1 if the arguments are unusable. The buffer must be EPD_FB_BYTES. */
int provscreen_render(uint8_t *fb, const char *ap_ssid, const char *pop);
