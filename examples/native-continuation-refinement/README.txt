AUTOMATIC NATIVE CONTINUATION REFINEMENT

Each .p demonstrates a normal form with an independent analytic crossing.
Fold: mu=0,x=0. Hopf: mu=0,angular frequency1. Period doubling: mu=0,
physical cycle period2*pi and critical Floquet multiplier-1.

auto_refine compiles/reuses native equation and AD2/variational callbacks.
The original branch and candidate flags survive every failed refinement.
branch.markers links every attempt to its candidate row and source interval.
branchplot renders stable/unstable/inconclusive segments and accepted crosses.

Hopf/period-doubling cubic criticality, fundamental orbit periods and folds of
cycles are not inferred by these augmented correctors; see the exact contract.
