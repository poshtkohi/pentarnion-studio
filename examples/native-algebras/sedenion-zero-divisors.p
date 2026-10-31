clc;
clear;

algebra S = builtin_algebra("sedenion");
% This exact zero-divisor pair uses Studio's documented Cayley-Dickson convention.
element a = S.e1 + S.e10;
element b = S.e4 + S.e15;
display("Nonzero a:", a);
display("Nonzero b:", b);
display("a*b, expected zero:", a*b);
assert(algebra_norm(a) > 0);
assert(algebra_norm(b) > 0);
assert(a*b == 0*S.one);

% The canonical reciprocal exists, but cannot make multiplication invertible.
element reciprocal = inv(a);
assert(algebra_norm(a*reciprocal-S.one) < 1e-12);
record equation = algebra_solveleft(a,0*S.one);
assert(!equation.has_solution);
assert(equation.rank < 16);
display("a*x=0: rank, conditioning and uniqueness report:", equation);
display("Division-algebra property:", algebra_info(S).properties.division);
display("Alternativity property:", algebra_info(S).properties.alternative);
% solveleft(a,0*S.one) deliberately reports an error: the solution is not unique.
