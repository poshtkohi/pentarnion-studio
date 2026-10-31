% Category 4: ordered Pentarnion entry kernels; no matrix-chain reassociation.
use_algebra("A_gamma",gamma=1);
clc;
clear;
threads(4);

vector base=ones(1,12000);
vector p=(1+i+j)*base;
vector q=(1-i+ell)*base;
vector pq=p.*q;
record product_run=parallel_info();
vector qp=q.*p;
assert(pq(1)!=qp(1));
assert(pq(1)==(1+i+j)*(1-i+ell));
display("p*q:",pq(1));
display("q*p:",qp(1));
display("Ordered-product worker report:",product_run);

vector whole_sine=sin(p);
assert(norm2(whole_sine(1)-sin(1+i+j))<1e-24);
display("Whole-element sin(p):",whole_sine(1));
display("Sine worker report:",parallel_info());
threads(0);
