#!/usr/bin/env python3
"""Emit the C ground-truth fixture for the web app's fixed-decimal formatter.

WHY THIS EXISTS. The device draws every value through C's `snprintf("%.*f")` and the editor previews
it through JavaScript. The two languages do not round a half the same way: printf rounds
half-to-EVEN (the FPU default), `toFixed` rounds half-away-from-zero. A weather reading lands on an
exact tie often — 72.5 °F at zero decimals — so the preview would show "73" where the glass shows
"72", which is the preview-is-a-confident-lie failure NFR-4 exists to prevent.

The fix is a BigInt implementation of printf's rule in src/data/format.ts. This script produces the
reference it is checked against, by actually running printf rather than by asserting what it ought
to do: the expected values are whatever the platform's libc prints, so the test cannot drift into
testing the same misunderstanding twice.

Output: test/fixtures/printf-fixed.json — a list of {value, decimals, out}. The `value` is written
with %.17g so the double survives the round trip EXACTLY; a shorter format would let the fixture
disagree with the JS value by one ULP, which is precisely the case this catches.

Regenerate (from code/webapp):
    python3 scripts/gen-printf-fixture.py
"""
import json
import os
import subprocess
import sys
import tempfile

# The values that matter: exact ties, values a hair off a tie, negative zero, very small
# magnitudes (where scaling by 10^d loses the distinction), negatives, and ordinary readings.
VALUES = [
    0.0, -0.0,
    0.5, 1.5, 2.5, 3.5, -0.5, -1.5, -2.5, -72.5, 72.5,
    0.05, 0.15, 0.25, 0.35, -0.05, 0.0005, 0.0000001, -0.0004,
    68.44, 68.45, 68.55, 72.55, 72.65, 12.345, 99.995, 100.005,
    1234.5678, -12.3456, 33.333333, 1.005, 2.675, 0.1 + 0.2,
    1e-2, 1e-3, 1e-4, 1e-5, 1e-6, 1e-7, -1e-7,
    999999.9999, 0.99999, 1.99999, 123456.789, 12345.6789, 98765.4321,
    0.123456, 0.654321, -0.654321,
    32.0, 32.05, 212.0, 98.6, 37.0, -40.0, -40.05,
    -1.0, -1.3999999999999999, 0.1, 0.2, 0.3,
]

C_SOURCE = r"""
#include <stdio.h>
#include <math.h>
int main(void) {
    double vals[] = {
""" + ",\n".join(f"        {v!r}" for v in VALUES) + r"""
    };
    const int nv = (int)(sizeof(vals) / sizeof(vals[0]));
    printf("[");
    int first = 1;
    for (int i = 0; i < nv; i++) {
        for (int d = 0; d <= 6; d++) {
            /* THE SIGN IS CARRIED SEPARATELY. JSON has no negative zero — a -0.0 value
             * serialises as 0 — so the sign would be lost on the way to the test and the
             * formatter would be blamed for a fixture defect. `negzero` records it explicitly,
             * using signbit() so the C side is the authority on whether the value is -0. */
            printf("%s{\"value\":%.17g,\"decimals\":%d,\"negzero\":%d,\"out\":\"",
                   first ? "" : ",", vals[i], d, signbit(vals[i]) ? 1 : 0);
            /* %.17g above is the VALUE as text; the string below is what printf draws for it. */
            printf("%.*f", d, vals[i]);
            printf("\"}");
            first = 0;
        }
    }
    printf("]\n");
    return 0;
}
"""


def main() -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    out_path = os.path.join(here, "..", "test", "fixtures", "printf-fixed.json")
    out_path = os.path.normpath(out_path)

    with tempfile.TemporaryDirectory() as td:
        src = os.path.join(td, "gen.c")
        exe = os.path.join(td, "gen")
        with open(src, "w") as fh:
            fh.write(C_SOURCE)
        cc = os.environ.get("CC", "cc")
        build = subprocess.run([cc, "-O0", "-o", exe, src], capture_output=True, text=True)
        if build.returncode != 0:
            sys.stderr.write(build.stderr)
            return 1
        run = subprocess.run([exe], capture_output=True, text=True)
        if run.returncode != 0:
            sys.stderr.write(run.stderr)
            return 1
        rows = json.loads(run.stdout)

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w") as fh:
        json.dump(rows, fh, indent=1)
        fh.write("\n")
    print(f"wrote {len(rows)} rows to {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
