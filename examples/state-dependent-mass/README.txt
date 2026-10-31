NATIVE TIME/STATE-DEPENDENT MASS OPERATORS

state-dependent-decay.p solves (1+y)y'=-(y+y^2), y(0)=1. Its exact answer is
y(t)=exp(-t). Both RHS and mass callbacks compile to reusable native modules;
the generated residual Jacobian includes the dM/dy*y' term automatically.

singular-mass-constraint.p uses a singular mass matrix without inverting it.
The second coordinate is algebraic, z=x^2, and IDAS solves the locally regular
index-one system. The adapter reports this local contract explicitly and does
not claim general higher-index reduction.

Run each .p directly or open project.pprj. See specs/mass_operators_v0_2.txt.
