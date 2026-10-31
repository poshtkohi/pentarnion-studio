EXACT ORDERED PENTARNION COEFFICIENTS

basis-and-grouping.p demonstrates the exact normalized multiplication table,
both bracketings, a nonzero associator, exact rational/radical cancellation and
a symbolic gamma=1/2 context. coordinate-derivatives.p declares scalar and
Pentarnion variables, inspects real-coordinate derivatives, substitutes an
exact rational and emits inspectable native-function source.

symexpand(text[,options(reals="a,b",pentarnions="p,q",gamma="1/2")])
returns an immutable record with c0..c4 exact scalar coefficient strings,
source_expression (the original grouped input), expression (expanded form),
gamma and explicit domain declarations. symcoeff(record,0..4) returns a string.
sympentdiff(record,"p0") differentiates one central real coordinate;
sympentsubs(record,"a","1/2") substitutes an exact real expression.
sympentnumeric(record[,options(real_coordinate=value,...)]) returns a
Pentarnion only if the record gamma matches the current workspace context.
sympenttext(record) shows the explicit coefficient expression.
sympentcode(record[,"function_name"]) returns a checked native function.

Declared Pentarnion p introduces independent central real p0..p4 coordinates.
Unknown symbols and conflicting declarations fail; no variable is implicitly
assumed real or associative. Product Groups are evaluated in their input order
before coefficient expansion; only the central real coefficient polynomials
are commuted/combined. This never applies associativity to Pentarnion products.
The original formal sym/symdiff/symsubs/symsimplify interfaces are unchanged.

Exact constants use bounded Boost multiprecision rationals and positive square
radicals. Gamma is an explicit reduced rational in[0,1], with numerator and
denominator at most1e6. Supported expression syntax is +,-,*,real constant
division, nonnegative integer powers0..32, sqrt of nonnegative rational
constants and Groups. Variable denominators, implicit equation division,
symbolic gamma, general functions, formal matrices and complex radical domains
are rejected until their assumptions/semantics are defined. No inverse/norm
identity or cancellation through possibly zero denominators is inferred.

The engine is a negotiated optional query in the existing lazy penta_science
DLL/SO, with bounded parser depth, rational size, term count, degree, output,
work and cancellation. Exact scalar field simplification is deterministic.
Numeric conversion uses extended accumulation and reports nonfinite results.
