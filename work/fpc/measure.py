import sys, numpy as np

def read_ppm(p):
    f = open(p, 'rb')
    assert f.readline().strip() == b'P6'
    line = f.readline()
    while line.startswith(b'#'):
        line = f.readline()
    w, h = map(int, line.split())
    maxv = int(f.readline())
    assert maxv == 255
    d = np.frombuffer(f.read(w * h * 3), dtype=np.uint8).reshape(h, w, 3)
    return d

img = read_ppm(sys.argv[1])
h, w, _ = img.shape
R, G, B = img[:, :, 0].astype(int), img[:, :, 1].astype(int), img[:, :, 2].astype(int)

cyan = (R < 120) & (G > 190) & (B > 190)          # dimension lines / text
red  = (R > 190) & (G > 60) & (G < 200) & (B > 60) & (B < 200) & (R - G > 40)
blk  = (R < 80) & (G < 80) & (B < 80)             # gold fingers / solid marks

print("image %dx%d  cyan %d  red %d  black %d" % (w, h, cyan.sum(), red.sum(), blk.sum()))

def runs(mask, axis, minlen):
    """Return list of (row_or_col, start, end) for runs longer than minlen."""
    out = []
    n = mask.shape[axis]
    for i in range(mask.shape[1 - axis]):
        line = mask[:, i] if axis == 0 else mask[i, :]
        idx = np.flatnonzero(line)
        if idx.size == 0:
            continue
        brk = np.flatnonzero(np.diff(idx) > 1)
        starts = np.r_[idx[0], idx[brk + 1]]
        ends = np.r_[idx[brk], idx[-1]]
        for s, e in zip(starts, ends):
            if e - s >= minlen:
                out.append((i, int(s), int(e), int(e - s + 1)))
    return out

def merge(lines, tol=3):
    """Merge runs on adjacent rows with similar extent (a line is ~2-3 px thick)."""
    lines.sort()
    groups = []
    for ln in lines:
        for g in groups:
            if abs(g[-1][0] - ln[0]) <= tol and abs(g[-1][1] - ln[1]) < 6 and abs(g[-1][2] - ln[2]) < 6:
                g.append(ln)
                break
        else:
            groups.append([ln])
    return [(float(np.mean([x[0] for x in g])),
             int(np.median([x[1] for x in g])),
             int(np.median([x[2] for x in g])),
             int(np.median([x[3] for x in g]))) for g in groups]

print("\n=== HORIZONTAL cyan lines (y, x0, x1, len) ===")
for y, x0, x1, L in sorted(merge(runs(cyan, 0, 90)), key=lambda t: -t[3]):
    print("  y=%6.1f  x %5d..%5d  len %5d px" % (y, x0, x1, L))

print("\n=== VERTICAL cyan lines (x, y0, y1, len) ===")
for x, y0, y1, L in sorted(merge(runs(cyan, 1, 90)), key=lambda t: -t[3]):
    print("  x=%6.1f  y %5d..%5d  len %5d px" % (x, y0, y1, L))
