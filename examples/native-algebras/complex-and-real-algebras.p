clc;
clear;

% The original complex type remains available alongside table-backed elements.
complex z = complex(2,3);
algebra C = builtin_algebra("complex");
element<C> w = 2 + 3*C.i;
display("Original complex product:", z*z);
display("Native algebra element product:", w*w);
assert(w*w == -5 + 12*C.i);
assert(conj(w) == 2 - 3*C.i);
assert(norm2(w) == 13);
assert(algebra_norm(w*inv(w)-C.one) < 1e-12);

algebra R = builtin_algebra("real");
element<R> x = 3;
element<R> y = element(R,[4]);
display("Real-algebra x*y:", x*y);
assert(coeff(x*y,0) == 12);
display("Complex algebra definition:", algebra_info(C));
