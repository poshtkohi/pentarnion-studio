clc;
clear;

algebra H = builtin_algebra("quaternion");
element x = 0.3 + 0.4*H.i;
element expected_exp = exp(0.3)*(cos(0.4)+sin(0.4)*H.i);
assert(algebra_norm(exp(x)-expected_exp) < 1e-12);
assert(algebra_norm(sin(x)*sin(x)+cos(x)*cos(x)-H.one) < 1e-11);
assert(algebra_norm(cosh(x)*cosh(x)-sinh(x)*sinh(x)-H.one) < 1e-11);
display("Quaternion x:", x);
display("exp(x), sin(x), cos(x):", exp(x), sin(x), cos(x));
display("sinh(x), cosh(x):", sinh(x), cosh(x));

algebra D = import_algebra("dual-numbers.json");
element argument = 0.3 + 0.4*D.eps;
assert(algebra_norm(exp(argument)-(exp(0.3)+0.4*exp(0.3)*D.eps)) < 1e-12);
assert(algebra_norm(sin(argument)-(sin(0.3)+0.4*cos(0.3)*D.eps)) < 1e-12);
display("Exact-table dual exponential:", exp(argument));

% These functions use the whole algebra element's one-element calculus.
% They are not separate applications to each coefficient. A validated unit
% and exactly proved power associativity are required for this API.
