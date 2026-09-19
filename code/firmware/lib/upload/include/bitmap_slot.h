#pragma once

#include <stddef.h>
#include <stdint.h>

/* The two static-bitmap slots and the atomic-promote rule (IF-2a).
 *
 * WHY TWO SLOTS AND A SEQUENCE NUMBER: the requirement is that an interrupted upload leaves
 * the PREVIOUS bitmap intact and active. Writing the new bitmap over the live one cannot
 * satisfy that — a power cut mid-write leaves neither image. So the new bitmap goes to the
 * spare slot, and only a tiny header write switches which slot is live.
 *
 * WHY NOT NVS: NVS here is 24 KB and the bitmap is 78,200 bytes, so each slot gets a raw
 * partition (see partitions.csv: `bitmap_a`, `bitmap_b`).
 *
 * The selection rule below is pure so it is host-testable (NFR-6) — "which slot survives a
 * torn write" is logic, and it is the part that decides whether a power cut bricks the
 * display. */

#define BITMAP_SLOT_MAGIC   0x454D4954u   /* "TIME" */
#define BITMAP_SLOT_LEN     78200u        /* 920x680 at 1 bpp (HW-6) */

typedef enum {
    BITMAP_SLOT_A = 0,
    BITMAP_SLOT_B = 1,
    BITMAP_SLOT_COUNT
} bitmap_slot_id_t;

/* Stored at the head of each slot's partition. Small, and the only thing rewritten on
 * promote, so a promote is a bounded write that either completes or does not. */
typedef struct {
    uint32_t magic;
    uint32_t len;       /* bytes of bitmap that follow */
    uint32_t crc;       /* CRC-32 of those bytes */
    uint32_t seq;       /* monotonically increasing; 0 means "never written" */
} bitmap_slot_hdr_t;

/* Is this header a usable bitmap? A slot that fails this must never be shown. */
int bitmap_slot_hdr_valid(const bitmap_slot_hdr_t *h);

/* Which slot is live, given both headers. Returns BITMAP_SLOT_A or BITMAP_SLOT_B.
 *
 * Rule: the valid slot with the HIGHER sequence number wins; ties go to A. A slot that
 * fails validation is ignored even if its sequence is higher — otherwise a torn write that
 * happened to leave a large garbage sequence in the header would be selected over a
 * perfectly good image. If NEITHER is valid, A is returned so the caller has a defined
 * answer and can report "no bitmap" rather than an arbitrary slot.
 *
 * This is what makes a power cut safe: the promote writes the spare slot's header with
 * seq = old + 1. If that write tears, the CRC check fails, the slot is ignored, and the
 * previously active image is still selected on the next boot. */
bitmap_slot_id_t bitmap_slot_pick(const bitmap_slot_hdr_t *a,
                                  const bitmap_slot_hdr_t *b);

/* The slot a promote should write to: the one that is NOT currently live. Writing over the
 * live slot would destroy the only good image. */
bitmap_slot_id_t bitmap_slot_spare(bitmap_slot_id_t live);

/* Compute the header for promoting `data` into `live`'s spare slot.
 *
 * `live_seq` is the sequence number of the currently live slot. The new header gets
 * live_seq + 1, which is what makes it win on the next boot. Returns 0 on success, or -1 if
 * `len` is not BITMAP_SLOT_LEN (a wrong-sized bitmap must never be promoted — it would leave
 * part of the panel showing the old image). */
int bitmap_slot_make_hdr(const uint8_t *data, uint32_t len, uint32_t live_seq,
                         bitmap_slot_hdr_t *out);

/* CRC over a whole bitmap. Same algorithm as bitmap_upload.h so the upload's checksum and
 * the slot's stored checksum are comparable. */
uint32_t bitmap_slot_crc32(const uint8_t *data, size_t len);
