% One Pentarnion residual supplies five real equations and generated derivatives.
use_algebra("A_gamma",gamma=1);clc;clear;
native function pentarnion residual(real t,pentarnion p,pentarnion dp,real rate){return dp+rate*p;}
pentarnion initial=1+2*i+3*j+4*k+5*ell;
record trajectory=daesolve(residual,initial,-2*initial,options(parameters=options(rate=2.0),algebraic=zeros(1,5),initialization="check",t1=.5,rtol=1e-9,atol=1e-11));
display("Five real coefficient columns (1,i,j,k,ell):",trajectory.values(end,:));
assert(norm(trajectory.values(end,:)-exp(-1)*[1,2,3,4,5],2)<1e-7);
