GENERAL CONSTRAINED MECHANICS AND PHYSICAL DIMENSIONS

moving-mass-and-constraint.p: L=(1+q^2)v^2/2, moving constraint q=t.
Exact q=t, v=1 and reaction lambda=t check the state-dependent mass, moving
constraint derivatives and energy supplied by constraint work independently.

velocity-constraint-and-work.p: L=(v1^2+v2^2)/2 and v2=q1 use the d'Alembert
velocity-constraint formulation. Exact q=(t,t^2/2) checks the trajectory and
reaction-work balance. This is a velocity constraint, not a vakonomic model.

check-physical-dimensions.p checks native fixed-array kinetic energy units and
checks a physical oscillator at the actual odesolve boundary. Dimensions are
coherent SI annotations; values and compilation fingerprints remain unchanged.

Both mechanics examples use cached native AD2/AD3 callbacks and IDAS; analytic
Jacobians include mass and constraint third derivatives. Initial constraints
must be consistent and the constrained block locally regular. Constraint drift
and trapezoidal work error are reported, with explicit local index-one scope.
See specs/general_constrained_mechanics_v0_2.txt and equation_units_v0_2.txt.
