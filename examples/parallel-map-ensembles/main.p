use_algebra("A_gamma", gamma=1);
clc; clear; close("all");

native function real contraction(real x,real target) {
    return 0.5*x+0.5*target;
}
matrix initials=linspace(-4,4,64)';
record serial=ensemble(contraction,initials,
    options(parameters=options(target=2.0),iterations=16,threads=1));
record parallel=ensemble(contraction,initials,
    options(parameters=options(target=2.0),iterations=16,threads=4));
assert(parallel.states==serial.states);
assert(parallel.failures==0&&parallel.escapes==0);
display("Native ensemble actual workers:",parallel.threads_used);
display("Serial/parallel final values agree exactly:",parallel.states);

% Failure isolation preserves valid neighbors and original row/task identities.
native function real reciprocal(real x) { return 1/x; }
record isolated=ensemble(reciprocal,[1;0;2],options(threads=3));
assert(isolated.success==[1;0;1]);
display("Independent run status; zero input fails without discarding neighbors:",
    isolated.success,isolated.messages);

% For a parameter scan, explicitly retain each parameter as an unchanged map
% state coordinate. No random stream or execution order is hidden in the map.
native function [real next,real parameter] logistic(real x,real r) {
    return r*x*(1-x),r;
}
matrix parameters=linspace(2.5,4.0,128)';
matrix starts=[0.2*ones(128,1),parameters];
record scanned=ensemble(logistic,starts,options(iterations=200,threads=4));
assert(scanned.failures==0);

figure(1,"Name","Native parallel experiments","NumberTitle","off");
subplot(1,2,1);plot(initials,parallel.states);
title("Independent contraction runs");xlabel("Initial state");ylabel("Final state");
subplot(1,2,2);plot(scanned.states(:,2),scanned.states(:,1));
title("Final logistic sample per parameter");xlabel("$r$");ylabel("$x_{200}$");
caption("A final-sample scan is not a bifurcation continuation or chaos certificate.");
