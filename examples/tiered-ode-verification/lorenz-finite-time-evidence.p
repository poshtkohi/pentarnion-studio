use_algebra("A_gamma",gamma=1);
clc;clear;close("all");
ode lorenz(t,real x,real y,real z) {
    x'=10*(y-x);
    y'=x*(28-z)-y;
    z'=x*y-(8.0/3)*z;
}
record screen=discover(lorenz,[1,1,1],
    options(t1=20.0,sample_step=.02,method="bdf",rtol=1e-8,atol=1e-10,
        transient=10.0,return_tolerance=.15,min_return_time=1.0));
tic;
record evidence=verifyexperiment(lorenz,screen,[1],
    options(horizons=[40,80],lyapunov=1,qr_step=.5,
        lyapunov_tolerance=.4,minimum_positive=.1,minimum_observation=20.0,
        max_native_operations=20000000));
toc;
display(evidence.labels);
display("Sorted finite-time spectra: ",evidence.exponents);
display("Window / method-QR / adjacent-tier differences: ",
    evidence.window_drift,evidence.settings_drift,evidence.tier_drift);
display("Convergence flags: ",evidence.window_converged,
    evidence.settings_converged,evidence.tier_converged);
display(evidence.notice);
record selected=verifyrun(lorenz,evidence,2);
figure(1,"Name","Lorenz finite-time evidence","NumberTitle","off");
subplot(1,2,1);plot(selected.t,selected.values(:,1));
title("Longer replay");xlabel("t");ylabel("x(t)");
subplot(1,2,2);plot(selected.values(:,1),selected.values(:,3));
title("Projection (x,z), not an orbit proof");xlabel("x");ylabel("z");
