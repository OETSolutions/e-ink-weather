# qrcodegen — QR Code generator (C)

Vendored from [Project Nayuki's QR Code generator](https://github.com/nayuki/QR-Code-generator),
`c/qrcodegen.h` and `c/qrcodegen.c`. **MIT License**, copyright (c) Project Nayuki.

## Why vendored rather than reimplemented

The provisioning screen needs a QR code whose payload includes the device's own name, which
contains its MAC address — so unlike the setup-page and app-store codes, it CANNOT be
pre-generated at build time. A QR encoder is therefore required on the target.

Writing one from scratch means Reed-Solomon error correction over GF(2^8), the eight mask
patterns and their penalty scoring, and the function-module layout — several hundred lines of
exactly the kind of code that produces a symbol that *looks* like a QR code and will not scan.
This library is small (about 1,000 lines), allocation-free, dependency-free, single-translation-
unit, and exhaustively tested upstream. Vendoring it is strictly better than a hand-rolled
version, and it is permissively licensed.

## Notes for use here

- `qrcodegen_getModule()` returns `true` for a DARK module. The two packed-array helpers in
  `lib/provscreen` treat 1 as dark, matching it.
- Buffers must be at least `qrcodegen_BUFFER_LEN_MAX` (3,918 bytes) for the version range used.
  Two are needed per encode (a temp buffer and the output), which is the largest stack cost on
  the provisioning path — see the caller for how that is sized.
- Upstream is unchanged apart from this file. To update, re-download the two files.
