clc;
clear;
close("all");

algebra H = builtin_algebra("quaternion");

% Descriptor, basis signs and coefficient widths are fixed at source checking.
% Input values and omega remain runtime data for the cached native DLL/SO.
native function element<H> spin(element<H> p,real omega) {
    return omega*(H.k*p);
}
element initial = H.one;
element velocity = spin(initial,2.0);
assert(velocity == 2*H.k);
matrix J = jacobian(spin,initial,options(parameters=options(omega=2.0)));
assert(norm(J-2*algebra_leftop(H.k),"fro") < 1e-12);
record checked = derivative_check(spin,initial,options(parameters=options(omega=2.0)));
assert(checked.passed);
display("Native quaternion RHS Jacobian:", J);
display("Independent derivative check:", checked);

ode rotation(t,element<H> q;real omega) {
    q' = omega*(H.k*q);
}
tic;
solution once = odesolve(rotation,q=initial,omega=1.0,
    t0=0,t1=6.283185307179586,sample_step=0.025,
    method="bdf",jacobian="analytic",rtol=1e-10,atol=1e-12);
toc;
display("omega=1; q(t)=cos(t)+k*sin(t):", once);

% The same body with a different runtime parameter reuses its compiled kernel.
solution twice = odesolve(rotation,q=initial,omega=2.0,
    t0=0,t1=6.283185307179586,sample_step=0.025,
    method="bdf",jacobian="analytic",rtol=1e-10,atol=1e-12);
display("omega=2; cached native body:", twice);

figure(1,"Name","Native quaternion ODE","NumberTitle","off");
subplot(2,2,1);plot(once.t,coeff(once.q,0));
title("Scalar coefficient, $\\cos(t)$");xlabel("$t$");ylabel("$q_0$");
subplot(2,2,2);plot(once.t,coeff(once.q,3));
title("$k$ coefficient, $\\sin(t)$");xlabel("$t$");ylabel("$q_3$");
subplot(2,2,3);plot(coeff(once.q,0),coeff(once.q,3));
title("One revolution: $\\omega=1$");xlabel("$q_0$");ylabel("$q_3$");axis("equal");
subplot(2,2,4);plot(twice.t,coeff(twice.q,3));
title("Faster rotation: $\\omega=2$");xlabel("$t$");ylabel("$q_3$");
