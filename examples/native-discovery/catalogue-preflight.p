use_algebra("A_gamma",gamma=1);
clc;clear;
ode force(t,pentarnion q,pentarnion v;pentarnion A,pentarnion B,real m,real a,real b) {
    q'=v;
    v'=(a*(A*q)+b*(B*(q*q)))/m;
}
matrix directions=pentdirections();
assert(norm(sum(directions.*directions,2)-ones(242,1))<1e-12);
record plan=discoverplan(force,zeros(1,10),options(parameters=options(
    A=searchset(directions),B=searchset(directions),m=1.0,a=1.0,b=1.0),
    positive_parameters="m,a,b"));
assert(plan.parameter_combinations==58564);
display("Directions:",size(directions));
display("One model / matched paired models:",plan.runs,2*plan.runs);
display("Fits default task budget:",plan.within_budget);
% Preflight does not compile or launch 58,564 tasks. Choose an explicit bounded
% subset before discover. The full paired count is 117,128, before other scans.
