use_algebra("A_gamma", gamma=1);
clc;
clear;

% Compiled once; point/parameter/derivative options do not rebuild the DLL/SO.
native function real objective(real x, real y, real a) {
    real residual = y - x*x;
    return a*residual*residual + (1.0-x)*(1.0-x);
}

options cfg = options(parameters=options(a=100.0), method="analytic", threads=2);
matrix point = [1.2, 1.4];
tic;
matrix g = gradient(objective, point, cfg);
matrix H = hessian(objective, point, cfg);
matrix Hv = hvp(objective, point, [0.3, -0.7], cfg);
toc;
display("Gradient:", g);
display("Hessian:", H);
display("Hessian-vector product:", Hv);
assert(abs(g(1)-19.6)<1e-10);
assert(abs(g(2)+8.0)<1e-10);
assert(abs(H(1,1)-1170.0)<1e-9);
assert(abs(H(1,2)+480.0)<1e-9);
assert(abs(Hv(1)-687.0)<1e-9);
assert(abs(Hv(2)+284.0)<1e-9);

record checked = derivative_check(objective, point,
    options(parameters=options(a=100.0), order=2));
display("Independent native finite-difference check:", checked);
assert(checked.passed);

% Selected parameter partial at fixed state; a is coordinate3, or use its name.
matrix Ja = jacobian(objective, point,
    options(parameters=options(a=100.0), wrt="a"));
display("Partial derivative with respect to a:", Ja);
assert(abs(Ja(1)-0.0016)<1e-12);

native function [real first, real second] residuals(real x, real y) {
    return x*x+y-3.0, 2.0*x-y*y+1.0;
}
matrix J = jacobian(residuals, [2.0, 3.0]);
matrix Jv = jvp(residuals, [2.0, 3.0], [2.0, -1.0]);
matrix Jtw = vjp(residuals, [2.0, 3.0], [2.0, -1.0]);
display("State Jacobian:", J);
display("J times v:", Jv);
display("J transpose times w:", Jtw);
assert(J==[4.0,1.0;2.0,-6.0]);
assert(Jv==[7.0;10.0]);
assert(Jtw==[6.0;8.0]);

% Closed-form flow example: initial-condition partial, not an ODE sensitivity solve.
native function real exact_flow(real y0, real t, real rate) {
    return y0*exp(-rate*t);
}
matrix initial_partial = jacobian(exact_flow, [2.0,1.0,0.5], options(wrt="y0"));
display("Initial-value partial of the explicit flow:", initial_partial);
assert(abs(initial_partial(1)-exp(-0.5))<1e-12);
