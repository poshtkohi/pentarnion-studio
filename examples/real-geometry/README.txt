REAL GEOMETRY

Run primitives-and-intersections.p for analytic primitives, real intersections
and native mesh plots. Run parametric-surface-in-five-coordinates.p to retain
and inspect every coefficient of a Pentarnion-valued parameterization.

Mesh records hold vertices (N by dimension), one-based triangular faces,
boundary edge rows [vertex1,vertex2,tag], coordinate kind and algebra context.
The induced Euclidean surface metric is used for geometric measurements.
meshplane assigns left/right/bottom/top tags1/2/3/4. Open cylinder ends use1/2.
paramesh derives boundary tag1; periodic axes require samples without a
duplicated endpoint. Transforms retain all2..5 resulting real coordinates.

meshinfo provides area, polygonal curve length, orientation/manifold checks,
quality measures, face areas and normal projectors. Unique unit normals and
signed enclosed volume are available only where dimension/topology allow them:
three coordinates, and a closed consistently oriented surface for volume.
Manifold/degeneracy checks do not prove global non-self-intersection.

meshrefine subdivides triangles, preserving boundary tags. Optional radius and
center radially project newly introduced midpoints. meshtransform applies an
explicit affine matrix and optional translation. meshplot projects selected
coordinates; this never changes the stored mesh.
meshplot(mesh) renders the geometry in the current series colour; scalar data
is optional. To colour a field, use meshplot(mesh,options(values=field_values))
with one finite real value per vertex. Plain geometry has no scalar colour bar
or scalar data-tip value. PNG/SVG/PDF exports support both forms.

meshwrite refuses an existing filename. .pmesh retains2..5 coordinates, tags,
and coefficient context. .obj accepts only the documented3-coordinate v/f
triangular subset plus comments and #penta_boundary records; UVs/normals,
negative indices and polygon conversion are deliberately rejected.
