PAPER SPIN GEOMETRY AND DYNAMICS

These educational scripts contain explicit definitions from the authors' original
algebra research. They use the Studio normalized A_gamma_v1 basis.
The fixed workspace gamma=1 matches the numerical algebra_gamma parameter.
Changing gamma requires changing both; multiplication is immutable within a run.

distinguished-space-and-energy.p constructs the paper's h_gamma/b/c frame and
normal, checks the six-product multiplication energy against its closed formula,
represents the real alternating three-form in (a,b,c,ell) coordinates and verifies
its oriented Hodge dual. The sphere embedding retains all five real coefficients.
The induced Euclidean Gaussian curvature is1: algebra-induced energy anisotropy
does not turn a rigidly tilted sphere into a different intrinsic geometry.

fixed-gamma-spin-dynamics.p directly compiles the paper's three real Lie-Poisson
equations and automatic Jacobian. The bracket is the ordinary real cross product
in the orthonormal adapted basis, with the paper's stated sign. The script checks
sampled spin-length and Hamiltonian errors. Numerical preservation is tolerance-
dependent; the autonomous unit-sphere model is not presented as a chaotic example.
Momentum/velocity/canonical Hamiltonian mechanics are not silently substituted.

All definitions and numerical parameters used here are explicit in the scripts.
No unpublished manuscript is included in this project. General algebra-dependent connection/torsion, time-varying
multiplication and switching sensitivity are distinct further formulations.
