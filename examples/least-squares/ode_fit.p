use_algebra("A_gamma", gamma=1);
clc;
clear;
close("all");

ode decay(t,real y;real rate) {
    y' = -rate*y;
}
vector times = [0,0.13,0.45,0.9,1.4,2.0];
matrix observations = (2.0*exp(-0.7*times))';

% The compiler emits J_x and J_theta. CVODES integrates the rate sensitivity
% and the initial-state sensitivity, which Ceres receives as its residual J.
tic;
record fitted = odefit(decay,1.0,times,observations,
    options(parameters=options(rate=0.2),fit_parameters="rate",fit_initial=1,
        lower=[0.01,0.1],upper=[3,5],integrator="bdf",
        jacobian="analytic",rtol=1e-10,atol=1e-12));
toc;
assert(fitted.converged);
assert(abs(fitted.parameters.rate-0.7)<1e-6);
assert(abs(fitted.initial-2.0)<1e-6);
display("Fitted rate (expected 0.7):",fitted.parameters.rate);
display("Fitted initial state (expected 2):",fitted.initial);
display("Jacobian source:",fitted.sensitivity_method);
display("Data residual norm:",fitted.residual_norm);

figure(10,"Name","Native ODE parameter and initial-state fitting","NumberTitle","off");
subplot(2,1,1);
plot(fitted.times,fitted.fitted);
title("Fitted trajectory: $y(t)=2e^{-0.7t}$");
xlabel("$t$"); ylabel("$y(t)$");
subplot(2,1,2);
plot(fitted.times,fitted.residuals);
title("Residuals at the exact observation times");
xlabel("$t$"); ylabel("$r(t)$");
