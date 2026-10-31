Implemented chart geometry and geodesics (second roadmap 12A/B/C, bounded stages)

Run plane_cylinder_sphere.p, great_circle_transport.p, or pentarnion_sphere_chart.p.
Charts take two real parameters (u,v) and optional trailing runtime parameters.
They return 2..5 real scalar outputs, or one Pentarnion whose coefficient order
is (1,i,j,k,ell). Compiler AD2 generates the first/second real derivatives in the
cached native callback. Changed points, radii or numerical options reuse a body.
The optional lazy module penta_geometry performs the geometry and path integration.
It is Qt-free; the existing sampled plotting/curvegeometry APIs are preserved.

chartgeometry(embedding,[u,v],options(metric="euclidean",parameters=...))
chooses g_ab=sum_mu (d_a X_mu)(d_b X_mu). This explicitly chosen Euclidean inner
product is independent of Pentarnion multiplication. gamma alone does not change
that metric or prove a curvature change. Results include point, tangent D-by-2,
metric/inverse_metric 2-by-2, christoffel 2-by-4, riemann 4-by-4, ricci 2-by-2,
gaussian_curvature, scalar_curvature=2K, and area_density=sqrt(det(g)). Connection
rows are upper index a; columns are lower pairs (11,12,21,22). Riemann flattening
rows=(a,b), columns=(c,d), with R^a_bcd=K(delta^a_c*g_bd-delta^a_d*g_bc),
Ric_bd=sum_a R^a_bad. Tensor positions in matrices use ordinary one-based indexing.
For induced geometry, second_fundamental is the normal-vector-valued B_ab,
D-by-4; mean_curvature_vector=(1/2)g^ab B_ab is orientation-independent as a vector.
For a sphere it points inward with this convention. Tangent/normal projectors
are D-by-D and preserve the full D-2 normal space; no single normal is invented
in codimension greater than one.

A native metric callback may instead return (E,F,G), i.e. g=[[E,F],[F,G]], with
metric_parameters as its separate trailing bindings. Its compiled AD2 derivatives
give intrinsic Levi-Civita curvature. extrinsic_available=0 in this case, and no
second_fundamental/mean_curvature_vector is returned: these cannot be inferred
from an independently supplied metric. Tangent/normal projectors still refer to
the display embedding's Euclidean tangent space, not to a hidden metric choice.
Positive definiteness and immersion are checked; metric_tolerance=1e-12 is the
minimum relative eigenvalue ratio, and degenerate/polar chart points fail clearly.

geodesic(embedding,[u,v],[du,dv],options(...)) integrates affine-parameter geodesics
of the selected metric and optional transport=[W^u,W^v] by native fixed-step RK4.
The embedding and metric bodies/derivatives are compiled, not interpreted per step.
Options include t0,t1,time_step,sample_step,max_steps,max_wall_seconds,
max_native_operations and lower/upper two-coordinate bounds. The method is serial
within Studio's shared CPU reservation. Change time_step for convergence; sampled
speed/transport drift are numerical diagnostics, not exact conservation guarantees.
Results include t, coordinates, velocity, all D embedded positions, transport,
speed_squared, transport_norm_squared, steps/evaluations/cache metadata. Coordinates
are chart components; transport uses the Levi-Civita connection, not arbitrary
torsion or an algebra-supplied connection. Leaving the selected chart or approaching
a numerical chart singularity stops with an error; automatic chart changes are
not implemented. Cancellation and work/time/output bounds apply.

Independent native tests cover plane/cylinder/sphere K, supplied sphere metrics,
R3-to-R5 isometries, normal-space rank/idempotence, great-circle trajectories,
meridian parallel transport and fourth-order path convergence. General chart
atlases, arbitrary connections, differential forms/Hodge/Laplace-Beltrami,
geometry-aware surface PDEs and paper-specific algebra-dependent frames remain
future stages. Full domain/mesh import/refinement/intersections are also pending.
