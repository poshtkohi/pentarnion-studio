NATIVE ALGEBRAS AND CUSTOM MULTIPLICATION TABLES

Open project.pprj in Studio and run any of its descriptive scripts.
The quaternion rotation script also draws a 3D trajectory. The other scripts
print calculations and check mathematical identities with assert.

Scripts
  quaternion-rotations-and-products.p: Hamilton products, ordered equation
    solving and a quaternion-generated rotation trajectory.
  complex-and-real-algebras.p: real/complex algebra elements alongside the
    existing complex type.
  octonion-bracketing.p: a nonzero associator and alternativity.
  sedenion-zero-divisors.p: two nonzero elements whose product is zero;
    verified reciprocal existence versus unique equation solving.
  dual-numbers-and-rigid-motion.p: polynomial differentiation with dual
    numbers and construction/extraction of a dual-quaternion rigid pose.
  clifford-and-split-algebras.p: Cl(2,1), split-complex zero divisors and
    split-quaternion signs.
  associative-lie-and-jordan-presets.p: matrix2, nonunital so(3) cross
    products and symmetric 2x2 Jordan products with a Frobenius metric.
  import-custom-multiplication-table.p: strict JSON imports, validated
    property evidence, arbitrary unit vectors and left equation solving.
  native-quaternion-jacobians-and-odes.p: cached native quaternion callback,
    generated 4x4 Jacobian, independent derivative check and ODE rotation
    with component plots. Requires Studio's bundled native compiler/ODE module.
  whole-element-calculus.p: quaternion and exact-table dual one-element
    sin/cos/exp/sinh/cosh, with scalar/complex subcase identities.
  import-csv-and-excel-products.p: native numeric CSV and XLSX product tables
    with separate floating metadata and exact raw dependency snapshots.
  prepare-csv-xlsx-tables.p: recreate the bundled numeric product files using
    Studio's native writer. Run this if either numeric example file was removed.
  algebra-vector-projections.p: four screenshot-ready panels with quaternion,
    sedenion e10/e15 and64-dimensional Clifford vector projections. Every
    arrow retains its complete coefficient vector for inspection/export.
  quaternion-roots-maps-and-fitting.p: native roots/fixed points, map iterates,
    Ceres coefficient fitting and an NLopt scalar objective with quaternion inputs.

Language
  algebra H = builtin_algebra("quaternion");
  element<H> q = 1 + 2*H.i - H.j;
  element r = element(H,[1,2,-1,0]);
  element s = element(H,1,2,-1,0);
  algebra A = import_algebra("dual-numbers.json");

Builtins: real, complex, quaternion, octonion, sedenion, dual,
dual_quaternion, split_complex, split_quaternion, clifford, matrix2,
lie_so3 and jordan_sym2.
Clifford signatures use literal options(p=positive,q=negative); at most
six generators (64 real coefficients). Other native families have fixed
dimensions. All elements carry an immutable definition, so similarly named
bases in different algebras do not mix implicitly. Existing Pentarnion i/j/k/ell
and the existing complex(...) type retain their original meanings.

Arithmetic: +, -, real scaling, ordered multiplication, integer powers and
explicitly ordered solveleft(a,b) for a*x=b / solveright(a,b) for x*a=b.
The report forms algebra_solveleft/right(a,b[,options(...)]) return rank,
conditioning, residual and an accepted unique coefficient solution only.
Options: rank_tolerance, condition_limit and residual_tolerance.
inv(a) verifies a two-sided reciprocal. In sedenions a reciprocal does not
establish unique left/right division; the zero-divisor example demonstrates it.

Utilities: conj, coeff(element,index0), commutator, associator, scalar, pure,
algebra_basis(A,index0), algebra_coefficients, algebra_norm, norm2,
algebra_leftop/rightop, algebra_info, algebra_json and algebra_products.
Indices in coeff/algebra_basis are zero-based. algebra_coefficients returns
a real row vector. Algebra operator matrices act on real coefficient columns.
scalar/pure require a unit equal to the first basis vector. norm2 uses an
explicit positive coordinate metric; it does not imply a composition norm.
algebra_norm always measures the Euclidean coordinate norm.

sin/cos/exp/sinh/cosh require a validated unit and exactly proved power
associativity. They use the whole element, not independent coefficients.
Numerical/declaration-only claims do not enable one-element calculus.

Conventions
Quaternion coefficients are (1,i,j,k), with i*j=k. Octonions and sedenions
extend this Hamilton convention using the documented ordered Cayley-Dickson
construction; algebra_info(...).convention records the precise convention.
Clifford basis order uses generator bitmaps: 1,e1,e2,e12,e3,... .
Other Fano/basis conventions require an explicit coordinate transformation.

Import format
The two JSON files are complete examples of StudioAlgebraDefinition version1.
Specify name, dimension, unique ASCII basis names, coefficient_domain
(rational or floating), omitted_products (zero or error) and products or tensor.
Sparse products contain left/right zero-based basis indices and terms of the
form [output_index,coefficient]. Rational strings such as "1/2" stay exact
during bounded identity verification; evaluation uses finite binary64 values.
unit is optional and may combine basis vectors. conjugation and a positive
metric are optional explicit matrices. Imported files contain data only.

algebra_info reports each property with value, evidence and explanatory
detail. Evidence can be proved_exact, numerical_evidence, failed, declared
or unknown. A user declaration is not a proof, and unverified division is
never assumed. Imports have byte/dimension/proof-work/cancellation limits.

Imports are captured during source checking. Their exact bytes participate
in source dependencies; the checked immutable descriptor is used during Run.
Table changes alter the descriptor hash for native callback cache invalidation.
Table names/paths and Clifford signature options must be literal so types
and native callback widths are known before execution.

algebra_json returns a portable JSON definition. algebra_products returns
numeric sparse rows [left1,right1,output1,coefficient] with ONE-based columns
for native CSV/XLSX export using writematrix. These exported index columns
are distinct from JSON's zero-based indices; do not substitute them directly.

To import those numeric rows use
  algebra A=import_algebra("metadata.json","products.csv");
  algebra B=import_algebra("metadata.json","products.xlsx","Products");
The metadata must use coefficient_domain=floating and products:[] with no tensor.
Each numeric row is[left1,right1,output1,coefficient]. Basis indices must be
one-based integers in range. Ambiguous overrides and duplicate terms reject.
The importer uses native numeric CSV/XLSX parsing without executing formulas.
Both metadata and product bytes participate in source dependency snapshots.
