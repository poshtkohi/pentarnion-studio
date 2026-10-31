pdes-and-geometry: REAL/PENTARNION PDE AND REAL GEOMETRY
Open project.pprj in Studio; run main.p first for the small real heat problem.
main.p compares centered FD and P1 consistent-mass FE against sin(pi*x)*exp(-nu*pi^2*t).
geometry.p shows imagesc/surf/contours/3D implicit sphere and a sampled circle.
paper_pde_eta0.p and paper_pde_eta1.p reproduce the attached report equation,
periodic N128 grid, L20,nu0.1,dt0.005,sample0.05,t_final120, deterministic perturbation.
Both preserve the exact binary reaction grouping. Runtime eta does not recompile it.
The initial RMS must be9.082951062292429e-4. The report's late mean RMS values are
0.924373(eta0) and2.10531e-4(eta1). Chaotic endpoint values need statistical/refinement
comparison rather than bitwise agreement; these finite runs do not prove chaos.
Plots downsample only their DISPLAY to every twentieth stored time; all2401x128x5
samples remain in field.values. Change eta in paper_pde_compute.p for a headless
penta-run computation without figure commands. Optional saveanalysis is data-only;
see reproducible-analysis for provenance export. Running a long example uses a bounded native
work budget distinct from interpreted statement work and reports actual counts.

Current PDE scope: one-dimensional scalar componentwise diffusion with native
real/Pentarnion local reactions; FD uniform grids, FE monotone grids. Periodic,
constant Dirichlet and homogeneous Neumann boundaries. Classical explicit RK4
with checked conservative diffusion limits; reaction stiffness still requires
smaller time_step and refinement. P1 reaction load uses a nodal interpolant M*F;
FE RMS/mean use lumped nodal quadrature. General nonlinear weak forms, Robin/time-
dependent boundaries, adaptive/stiff PDE integration,2D/unstructured FEM and
PETSc/MFEM backends remain later extensions, not claimed by this example.
