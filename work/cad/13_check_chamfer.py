import FreeCAD as App, Part, os, math, sys
HERE=os.path.dirname(os.path.abspath(__file__))
d=App.openDocument(os.path.join(HERE,"case_shell.FCStd"))
s=d.getObject("CASE_SHELL").Shape
# Rear taper occupies z 0..8. Classify side faces spanning that region; exclude end caps.
taper=[]
for f in s.Faces:
 b=f.BoundBox
 if b.ZMin<0.01 and b.ZMax>7.99 and b.ZLength>7.9:
  taper.append(f)
print("rear taper side faces",len(taper))
tri=[]
for i,f in enumerate(taper):
    typ=f.Surface.__class__.__name__
    # Polygonal triangles have exactly 3 vertices. Ruled rounded taper should have quadrilateral
    # planar sides or curved corner patches, never 3-vertex transition wedges.
    verts=len(f.Vertexes); edges=len(f.Edges)
    print(" face",i,"type",typ,"verts",verts,"edges",edges,"area",round(f.Area,2))
    if verts==3:tri.append(i)
if tri:
 print("FAIL triangular taper faces",tri);sys.exit(1)
if len(taper)!=8:
 print("FAIL expected 8 corresponding taper faces, got",len(taper));sys.exit(1)
print("CHAMFER TOPOLOGY PASS: 8 matched faces, 0 triangular transitions")
