clc;
clear;

% Full real 2x2 matrices: associative, with unit E11+E22.
algebra M = builtin_algebra("matrix2");
element<M> identity = 1;
assert(identity == M.E11+M.E22);
assert(M.E12*M.E21 == M.E11);
assert(M.E21*M.E12 == M.E22);
element a = 2*M.E11 + M.E12 + 3*M.E22;
display("Matrix-algebra reciprocal:", inv(a));
assert(algebra_norm(a*inv(a)-identity) < 1e-12);

% The so(3) Lie product is the cross product; it has no multiplicative unit.
algebra L = builtin_algebra("lie_so3");
element u = L.x + 2*L.y;
element v = L.y + L.z;
element w = L.x - L.z;
assert(L.x*L.y == L.z);
assert(L.y*L.x == -L.z);
element jacobi = (u*v)*w + (v*w)*u + (w*u)*v;
assert(algebra_norm(jacobi) == 0);
display("Lie bracket u*v:", u*v);
display("Jacobi identity, expected zero:", jacobi);
display("Lie unit evidence:", algebra_info(L).properties.unital);

% Symmetric 2x2 matrices with Jordan product a*b=(ab+ba)/2.
algebra J = builtin_algebra("jordan_sym2");
element<J> jordan_identity = 1;
assert(jordan_identity == J.E11+J.E22);
assert(J.S12*J.S12 == jordan_identity);
assert(J.E11*J.S12 == 0.5*J.S12);
assert(norm2(J.S12) == 2);
assert(associator(J.E11,J.S12,J.S12) == -0.5*J.E11+0.5*J.E22);
display("Jordan nonzero associator:", associator(J.E11,J.S12,J.S12));
display("Jordan capability evidence:", algebra_info(J).properties);
