use_algebra("A_gamma", gamma=1);
clc;
clear;
close("all");

% Every solver callback compiles to a cached native DLL/SO before iteration.
% Tolerances, initial guesses, brackets and runtime parameters reuse its cache.
native function real scalar_equation(real x, real a) {
    return x*x-a;
}
tic;
record scalar_root = root(scalar_equation, [0,2],
    options(parameters=options(a=2.0), xtol=1e-12, ftol=1e-10));
real solve_seconds = toc();
assert(scalar_root.converged);
assert(abs(scalar_root.x-sqrt(2))<1e-9);
display("sqrt(2), native TOMS748:", scalar_root);
display("Execution seconds (compilation excluded):", solve_seconds);
record changed_parameter = root(scalar_equation, [0,3],
    options(parameters=options(a=3.0)));
assert(changed_parameter.cache_hit);
display("Changed runtime parameter, same compiled callback:", changed_parameter.x);

% The compiler emits the complete 2-by-2 real-coordinate Jacobian.
native function [real first,real second] system_equations(real x,real y) {
    return x*x+y*y-2.0, x-y;
}
record system_root = rootsolve(system_equations, [0.8,0.5],
    options(method="line_search", jacobian="analytic",
            variable_scale=[1,1], residual_scale=[1,1]));
assert(system_root.converged);
assert(norm(system_root.x-[1,1])<1e-8);
display("Native KINSOL system, expected [1,1]:", system_root);

% Five real equations retain the specified left/right Pentarnion product.
native function pentarnion left_equation(pentarnion p,pentarnion a,pentarnion b) {
    return a*p-b;
}
native function pentarnion right_equation(pentarnion p,pentarnion a,pentarnion b) {
    return p*a-b;
}
pentarnion target = 1+i-j+0.25*ell;
pentarnion a = 2+0.25*j;
pentarnion b_left = a*target;
pentarnion b_right = target*a;
record left_root = rootsolve(left_equation, 0.0,
    options(parameters=options(a=a,b=b_left)));
record right_root = rootsolve(right_equation, 0.0,
    options(parameters=options(a=a,b=b_right)));
assert(left_root.converged && right_root.converged);
assert(left_root.residual_norm<1e-9 && right_root.residual_norm<1e-9);
pentarnion p_left = left_root.x;
pentarnion p_right = right_root.x;
display("Target:", target);
display("a*p=b_left:", p_left);
display("p*a=b_right:", p_right);
display("Different ordered right-hand sides:", b_left, b_right);

% Fixed point solves the compiled residual G(p)-p=0, not G(p)=0.
native function pentarnion affine_map(pentarnion p,pentarnion forcing) {
    return 0.5*p+forcing;
}
record map_fixed_point = fixedpoint(affine_map, i,
    options(parameters=options(forcing=1.0+j)));
assert(map_fixed_point.converged);
display("Map fixed point, expected 2+2*j:", map_fixed_point.x);

% Equilibrium compiles an autonomous ODE RHS directly; no time integration.
ode driven_decay(t,real y;real rate,real forcing) {
    y' = -rate*y+forcing;
}
record steady_state = equilibrium(driven_decay, 0.0,
    options(parameters=options(rate=2.0,forcing=6.0)));
assert(steady_state.converged);
assert(abs(steady_state.x-3)<1e-9);
display("ODE equilibrium, expected 3:", steady_state);

% Nonconvergence is a result to inspect, never silently labelled a root.
native function real no_real_root(real x) {
    return x*x+1.0;
}
record unsuccessful = rootsolve(no_real_root, 0.0);
assert(!unsuccessful.converged);
display("Expected nonconvergence: x*x+1 has no real root.");
display("Status:",unsuccessful.status);
display("Residual norm (not an error code):",unsuccessful.residual_norm);
display("KINSOL detail (zero Jacobian at initial x=0):",unsuccessful.message);

figure(1,"Name","Category 8: roots and ordered equations","NumberTitle","off");
subplot(1,2,1);
vector x_samples = linspace(0,2,201);
vector residual_samples = x_samples.^2-2;
plot(x_samples,residual_samples);
title("Scalar equation $x^2-2=0$");
xlabel("$x$");ylabel("$F(x)$");
subplot(1,2,2);
plot3("i","j","ell",target,b_left,b_right);
title("Ordered Pentarnion equations");
xlabel("$i$");ylabel("$j$");zlabel("$\\ell$");
caption("$ap=b_L,\\quad pa=b_R$");
