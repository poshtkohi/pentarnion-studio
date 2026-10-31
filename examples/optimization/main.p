use_algebra("A_gamma", gamma=1);
clc;
clear;
close("all");

native function real rosenbrock(real x,real y) {
    return (1.0-x)*(1.0-x)+100.0*(y-x*x)*(y-x*x);
}
tic;
record local = minimize(rosenbrock,[-1.2,1.0],
    options(algorithm="lbfgs",jacobian="analytic",max_evaluations=2000));
toc;
assert(local.converged);
assert(norm(local.x-[1,1])<1e-5);
display("Native LBFGS Rosenbrock minimum, expected [1,1]:",local.x);
display("Objective:",local.objective);

native function real bowl(real x,real y) {
    return (x-2.0)*(x-2.0)+(y-2.0)*(y-2.0);
}
native function real inequality(real x,real y) {
    return x+y-1.0;
}
native function real equality(real x,real y) {
    return x-y;
}
record constrained = minimize(bowl,[0,0],options(algorithm="slsqp",
    inequality=inequality,equality=equality,lower=[-3,-3],upper=[3,3]));
assert(constrained.converged);
assert(constrained.feasible);
assert(norm(constrained.x-[0.5,0.5])<1e-6);
display("Native SLSQP constrained result, expected [0.5,0.5]:",constrained.x);
display("Independently checked constraint violation:",constrained.constraint_violation);

% Global searches require finite bounds and record their seed. Exhausting a
% finite search budget is reported separately from convergence/global proof.
native function real search_objective(real x) {
    return (x-0.25)*(x-0.25);
}
record global = minimize(search_objective,0.9,
    options(algorithm="crs2",lower=-1.0,upper=1.0,seed=42,max_evaluations=1000));
assert(abs(global.x-0.25)<0.01);
display("Seeded global-search candidate:",global.x);
display("Search termination:",global.status);

figure(12,"Name","Native NLopt optimization","NumberTitle","off");
subplot(2,1,1);
plot(local.history(:,1),local.history(:,2));
title("Rosenbrock objective history"); xlabel("Native callback requests"); ylabel("Objective");
subplot(2,1,2);
plot(constrained.history(:,1),constrained.history(:,3));
title("Nonlinear constraint violation history"); xlabel("Native callback requests"); ylabel("Violation");
