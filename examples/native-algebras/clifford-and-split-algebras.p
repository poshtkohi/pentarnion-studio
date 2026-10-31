clc;
clear;

algebra Cl = builtin_algebra("clifford",options(p=2,q=1));
% Bitmap basis: one,e1,e2,e12,e3,e13,e23,e123.
assert(Cl.e1*Cl.e1 == Cl.one);
assert(Cl.e2*Cl.e2 == Cl.one);
assert(Cl.e3*Cl.e3 == -Cl.one);
assert(Cl.e1*Cl.e2 == Cl.e12);
assert(Cl.e2*Cl.e1 == -Cl.e12);
display("Cl(2,1) product (e1+e3)*(e2+e3):", (Cl.e1+Cl.e3)*(Cl.e2+Cl.e3));
display("Clifford convention:", algebra_info(Cl).convention);

algebra Split = builtin_algebra("split_complex");
element u = 1 + Split.j;
element v = 1 - Split.j;
assert(Split.j*Split.j == Split.one);
assert(u*v == 0*Split.one);
display("Split-complex zero divisors:", u, v, u*v);

algebra Q = builtin_algebra("split_quaternion");
assert(Q.i*Q.i == -Q.one);
assert(Q.j*Q.j == Q.one);
assert(Q.k*Q.k == Q.one);
assert(Q.i*Q.j == Q.k);
display("Split-quaternion product:", (1+Q.i+Q.j)*(1-Q.k));
% norm2 is the explicitly stored Euclidean coordinate metric, not a split norm.
display("Coordinate metric squared of i+j:", norm2(Q.i+Q.j));
