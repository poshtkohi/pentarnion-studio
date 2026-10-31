% Global variance-based indices for independent uniform inputs.
use_algebra("A_gamma",gamma=1);clc;clear;close("all");
real pi=3.141592653589793;
native function real response(real x,real y,real z,real a,real b){return sin(x)+a*sin(y)*sin(y)+b*z^4*sin(x);}
record indices=sensitivity(response,[-pi,-pi,-pi],[pi,pi,pi],options(parameters=options(a=7.0,b=.1),method="sobol",samples=8192,seed=123,bootstrap=64,confidence=.95,threads=4));
display("Saltelli first-order estimates:",indices.first_order);
display("Jansen total-order estimates:",indices.total_order);
display("Matched-row bootstrap first-order intervals:",indices.first_lower,indices.first_upper);
display("Matched-row bootstrap total-order intervals:",indices.total_lower,indices.total_upper);
display("Mean / variance / native evaluations:",indices.mean,indices.variance,indices.evaluations);
% x3 can have negligible first-order effect and a nonzero interaction total.
figure(45,"Name","Global input effects: finite-sample estimates","NumberTitle","off");
subplot(1,2,1);plot([1,2,3],indices.first_order);title("First-order indices");xlabel("Input coordinate");ylabel("Estimated variance fraction");
subplot(1,2,2);plot([1,2,3],indices.total_order);title("Total-order indices");xlabel("Input coordinate");ylabel("Estimated variance fraction");
