import sys, numpy as np

def read_ppm(p):
    f = open(p, 'rb')
    assert f.readline().strip() == b'P6'
    line = f.readline()
    while line.startswith(b'#'):
        line = f.readline()
    w, h = map(int, line.split())
    f.readline()
    return np.frombuffer(f.read(w * h * 3), dtype=np.uint8).reshape(h, w, 3)

img = read_ppm(sys.argv[1])
h, w, _ = img.shape
R, G, B = [img[:, :, i].astype(int) for i in range(3)]
red = (R > 180) & (R - G > 50) & (R - B > 50)
blk = (R < 80) & (G < 80) & (B < 80)
cyan = (R < 120) & (G > 190) & (B > 190)

def colprofile(mask, name, step=4):
    print("=== %s: per-column y-extent ===" % name)
    cols = np.flatnonzero(mask.any(axis=0))
    if cols.size == 0:
        print("  none"); return
    print("  x range %d..%d" % (cols.min(), cols.max()))
    for x in range(cols.min(), cols.max() + 1, step):
        ys = np.flatnonzero(mask[:, x])
        if ys.size:
            print("   x=%4d  y %4d..%4d  span %4d" % (x, ys.min(), ys.max(), ys.max() - ys.min() + 1))

colprofile(red, "RED FPC outline", 6)
colprofile(blk, "BLACK gold fingers", 6)

# cyan calibration marks: outermost x of cyan in each row band
print("\n=== CYAN row profile (to locate dimension endpoints) ===")
rows = np.flatnonzero(cyan.any(axis=1))
print("  y range %d..%d" % (rows.min(), rows.max()))
for y in range(rows.min(), rows.max() + 1, 5):
    xs = np.flatnonzero(cyan[y, :])
    if xs.size:
        print("   y=%4d  x %4d..%4d  n=%4d" % (y, xs.min(), xs.max(), xs.size))
