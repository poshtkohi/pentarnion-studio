% Refine a full-state cycle and continue it through a runtime parameter.
% Analytic reference: radius=sqrt(mu), period=2*pi,
% Floquet multipliers 1 and exp(-4*pi*mu).
use_algebra("A_gamma", gamma=1);
clc;
clear;
close("all");

ode radial_cycle(t, real x, real y; real mu) {
    x' = (mu-x*x-y*y)*x-y;
    y' = x+(mu-x*x-y*y)*y;
}

tic;
record orbit = periodicorbit(radial_cycle, [1.05, 0],
    options(parameters=options(mu=1.0), period=6.1,
        method="adams", rtol=1e-10, atol=1e-12,
        tolerance=1e-7, sample_step=0.03));
toc;
display("Shooting status: ", orbit.status);
display("Refined period (expected 2*pi): ", orbit.period);
display("Full-state closure / phase residual: ", orbit.closure_residual, orbit.phase_residual);
display("Floquet real / imaginary parts: ", orbit.multipliers_real, orbit.multipliers_imag);
display("Autonomous neutral multiplier error: ", orbit.neutral_error);
display("Numerical transverse stability: ", orbit.stable);

record branch = continueperiodic(radial_cycle, orbit.x,
    options(parameters=options(mu=1.0), parameter="mu",
        period=orbit.period, points=5, step=0.1,
        method="adams", rtol=1e-10, atol=1e-12,
        tolerance=1e-7));
display("Periodic pseudo-arclength continuation: ", branch.status);
display("Parameter / period / closure: ", branch.parameter, branch.period, branch.closure_residuals);

figure(1, "Name", "Periodic shooting and Floquet diagnostics", "NumberTitle", "off");
subplot(2,2,1);
plot(orbit.states(:,1), orbit.states(:,2));
title("Refined full-state cycle"); xlabel("$x$"); ylabel("$y$");
subplot(2,2,2);
plot(orbit.t, orbit.states(:,1));
title("Cycle time series"); xlabel("$t$"); ylabel("$x(t)$");
subplot(2,2,3);
plot(orbit.history(:,1), orbit.history(:,3));
title("Scaled closure convergence"); xlabel("Newton iteration"); ylabel("Closure residual");
subplot(2,2,4);
plot(branch.parameter, branch.period);
title("Periodic branch"); xlabel("$\\mu$"); ylabel("Period");

figure(2, "Name", "Periodic branch stability", "NumberTitle", "off");
subplot(1,2,1);
plot(branch.parameter, branch.x(:,1));
title("Fixed phase section of the branch"); xlabel("$\\mu$"); ylabel("Section $x$");
subplot(1,2,2);
plot(branch.parameter, branch.measure);
title("Largest transverse multiplier modulus"); xlabel("$\\mu$"); ylabel("$|\\rho|$");
tilefigures("studio", 1, 2);
