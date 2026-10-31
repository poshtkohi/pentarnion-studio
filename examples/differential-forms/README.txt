REAL DIFFERENTIAL FORMS AND CHART OPERATORS

dform(n,k,coefficients[,options(metric=G,orientation=1)]) creates a constant
real k-form, n=1..8. The SPD metric and orientation are explicit. Coefficients
use lexicographically ordered increasing tuples: for n=4,k=2 the order is
(1,2),(1,3),(1,4),(2,3),(2,4),(3,4). Degree-zero forms have one coefficient.

formfield(@fn,point,k[,options(parameters=...,metric=G,orientation=1)])
compiles a pure real/tuple coefficient function to the native cache and obtains
its value/Jacobian/Hessian analytically. Trailing parameters are runtime data.
wedge, exteriord and hodge operate on typed form records. exteriord consumes
one known derivative order; d(d(f)) uses the generated Hessian. Constant forms
have known zero jets. Inputs to pointwise products must share metric,
orientation and nonconstant evaluation point.

The Hodge star uses the supplied pointwise real SPD metric. For dynamic fields
its output has no derivative jets: exteriord(hodge(dynamic_field)) is rejected,
because variable-metric derivatives have not been supplied. This prevents an
incorrect implicit assumption of constant metric. General differential forms
on arbitrary manifolds, indefinite/complex metrics and Pentarnion-valued wedge
products are not included by this explicitly real API.

laplacebeltrami(@embedding,@scalar_field,[u,v][,options]) evaluates the genuine
two-dimensional chart operator g^ab(f_ab-Gamma^c_ab f_c). Both functions use
native analytic derivatives. It accepts the same embedding parameters/custom
metric options as chartgeometry plus field_parameters for the scalar field.
All2..5 embedding coordinates are retained; singular charts are rejected.
