% Independent manufactured solution: u=1.5+cos(pi*x), with zero normal flux.
native function real forcing(real x,real y) {
    real pi=3.14159265358979323846;
    return pi*pi*cos(pi*x);
}
native function real exact(real x,real y) {
    real pi=3.14159265358979323846;
    return 1.5+cos(pi*x);
}
native function [real x,real y] plane(real u,real v) {return u,v;}
record mesh=paramesh(plane,linspace(0,1,9),linspace(0,1,9));
record field=femsolve(mesh,forcing,options(order=3,mean=1.5,exact=exact,threads=4));
display("Pure-Neumann compatible load / weighted mean error: ",field.compatibility_residual,field.mean_error);
display("Independent L2 error: ",field.l2_error);
display("CPU policy: ",field.thread_policy);
display("Workers / parallel operations: ",field.threads_used,field.parallel_calls);
figure(1,"Name","Mean-constrained high-order FEM","NumberTitle","off");
femplot(field);title("P3 pure-Neumann solution with prescribed mean");
