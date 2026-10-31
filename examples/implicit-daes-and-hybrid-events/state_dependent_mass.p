% A state-dependent mass enters naturally through an implicit residual.
use_algebra("A_gamma",gamma=1);clc;clear;
native function real residual(real t,real y,real dy){return (1+y*y)*dy+y;}
record trajectory=daesolve(residual,1.0,-.5,options(algebraic=[0],initialization="check",t1=1.0,rtol=1e-9,atol=1e-11));
real y=trajectory.values(end,1);
display("Final state and independent integrated residual:",y,log(1/y)+.5*(1-y*y)-1);
assert(abs(log(1/y)+.5*(1-y*y)-1)<1e-7);
display("Compiler Jacobian selection / local index contract:",trajectory.jacobian,trajectory.index_contract);
