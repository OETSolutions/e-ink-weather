#pragma once

/* Presence probe for the GT30L32S4W (Genitop) font ROM on the shared SPI bus (U8).
 *
 * The schematic silkscreens this part "(Reserve)" — a DNP/reserve footprint — so the
 * firmware must never assume it is there. The GT30 has NO read-ID command: its entire
 * command set is 0x03 (read) and 0x0B (fast read). Presence therefore cannot be
 * established by identification; it is established by reading a glyph whose contents
 * are known and comparing byte-for-byte, which is exactly what LibDriver's own
 * `gt30l32s4w_init()` self-test does.
 *
 * CS is GPIO15, decoded from the schematic's `SPI_CS_GT30` net landing on the ESP32
 * module's A13/T3 pin. Data returns on SPI_MISO = GPIO19, which the sheet wires to the
 * GT30's pin 2 (SO).
 *
 * `gt30_present()` probes at most once per boot and caches. Callers MUST treat 0 as a
 * normal, expected outcome and fall back to the flash font atlas (FR-14) — never as an
 * error, and never with a retry loop. */
int gt30_present(void);
