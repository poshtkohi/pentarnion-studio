EXPLICIT SI QUANTITIES

conversions-and-dimensional-checks.p and force-energy-and-array-units.p preserve
the original real integer-dimension API.
rational-dimensions-and-real-roots.p demonstrates exact rational SI dimensions
and odd/even real root branches.
absolute-temperatures-and-log-levels.p uses explicit absolute/delta temperatures,
dBm/dBV levels and linear reference conversions.
whole-quaternion-physical-quantities.p retains typed quaternion coefficients
through conversion, ordered products and equation division.

quantity records are explicit. unitvalues extracts magnitudes deliberately;
ordinary arithmetic and native AD do not strip units. Products on quantity arrays
are entrywise. Nonlinear absolute/logarithmic units require real values. Integer
whole-algebra powers preserve ordered multiplication; fractional algebra roots
reject. Distinct algebra definitions cannot mix. K_abs is absolute Kelvin;
K/delta_K are linear Kelvin deltas. Log levels must convert to linear values
before addition/products. All units remain opt-in.

The lazy native units DLL shares Studio's CPU thread budget for real array work.
See specs/physical_quantities_v0_2.txt for bounds and precise semantics, and
specs/equation_units_v0_2.txt for independent equation-level SI checks.
