DOMAIN-AWARE NATIVE SYMBOLICS

variable-gamma-and-associators.p expands the actual five-dimensional table
with symbolic gamma, differentiates its normalization/mixing coefficients,
and preserves nonassociative bracketing. Specialization produces checked
fixed-context native source for the existing shared-library cache.

assumptions-functions-and-complex-roots.p shows explicit positive/nonzero
domains, product/quotient/chain rules, declared formal functions and principal
complex radicals. Numeric conversion rechecks all domain assumptions.

conditional-symbolic-matrix-equations.p solves a central symbolic matrix
equation and prints its nonzero pivot conditions. These conditions are part
of the result, not an unconditional uniqueness claim. The emitted source
retains runtime guards and uses the same typed native compiler as ODEs.

The penta_cas module loads on demand. Existing symexpand/sympent* commands keep
their original exact polynomial/radical contracts. This bounded new engine
does not promise all Maple identities, nonlinear symbolic equation solving
or automatic nonassociative matrix inverses. Read domain_symbolics_v0_2.txt.
