% Planning checks native callback shape but does not compile or solve it.
native function real decay(real u,real t,real x,real rate) { return -rate*u; }
matrix x=linspace(0,1,9);
matrix initials=[ones(1,9);2*ones(1,9)];
record plan=pdeensemble(decay,x,initials,options(scan=options(rate=[.5,1,2]),gammas=[.25,1],pairing="cartesian",dry_run=1));
assert(plan.runs==12&&plan.compiled_models==0);
display("Columns: initial row, parameter set, gamma index",plan.source_indices);
