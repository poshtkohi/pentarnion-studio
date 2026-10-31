Bounded higher-index descriptor examples (extension10)

index-two-hidden-constraint.p includes an independent finite decay plus a
nilpotent algebraic chain, with exact manufactured x=omega*cos and z=sin.

index-three-consistent-initial-sensitivities.p reconstructs two hidden derivatives
using compiled time jets through AD3. Parameter probes repeat the complete native
consistency projection and solve; the initial y0/yp0 Jacobians are explicitly
finite-difference results, not an unavailable analytic mixed fourth derivative.

local-residual-rank-diagnostics.p reports native Jy/Jyp and local index1 evidence,
then correctly refuses a general higher-index classification from one point.

All callbacks compile/cache as DLL/SO functions. Changing E/A/parameters/settings
does not compile a new forcing body. Singular/ambiguous/ill-conditioned pencils
and indices above3 are rejected. See specs/higher_index_linear_dae_v0_2.txt.
