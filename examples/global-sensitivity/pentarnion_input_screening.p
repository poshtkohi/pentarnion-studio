% Five coefficient coordinates are independent declared inputs of one Pentarnion.
use_algebra("A_gamma",gamma=1);clc;clear;
native function real summary(pentarnion p){return coeff(p,0)+2*coeff(p,1)+3*coeff(p,2)+4*coeff(p,3)+5*coeff(p,4);}
record result=sensitivity(summary,[0,0,0,0,0],[1,1,1,1,1],options(method="morris",samples=64,seed=123,threads=4));
display("Coefficient order (1,i,j,k,ell), normalized effects:",result.mu);
assert(norm(result.mu-[1,2,3,4,5],2)<1e-10);
% Define a real summary explicitly; the engine never discards Pentarnion components.
