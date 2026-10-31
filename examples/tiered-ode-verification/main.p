use_algebra("A_gamma",gamma=1);
clc;clear;close("all");

% A nondegenerate unit-radius normal-form cycle. Analytic period=2*pi;
% transverse multiplier=exp(-4*pi). Every numerical equation remains native.
ode radial_cycle(t,real x,real y) {
    x'=(1-x*x-y*y)*x-y;
    y'=x+(1-x*x-y*y)*y;
}
record screen=discover(radial_cycle,[1,0;0,1],
    options(t1=7.0,sample_step=.02,method="bdf",rtol=1e-8,atol=1e-10,
        return_tolerance=.01,min_return_time=1.0,threads=4));
record evidence=verifyexperiment(radial_cycle,screen,[2,1],
    options(horizons=[20,40],rtol=1e-9,atol=1e-11,
        refine_periodic=1,periodic_tolerance=1e-8,threads=4));
display("Original run IDs / tier IDs: ",evidence.run_ids,evidence.tier_ids);
display("Finite-time labels: ",evidence.labels);
display("Periods / closure residuals: ",evidence.periods,evidence.closure_residuals);
display("Floquet moduli (one neutral): ",evidence.floquet_moduli);

% Evidence row3 is original run2 at the second, longer horizon.
record selected=verifyrun(radial_cycle,evidence,3);
figure(1,"Name","Tiered cycle verification","NumberTitle","off");
subplot(1,2,1);plot(selected.t,selected.values(:,1));
title("Longer native replay: x(t)");xlabel("t");ylabel("x");
subplot(1,2,2);plot(selected.values(:,1),selected.values(:,2));
title("Full two-coordinate phase portrait");xlabel("x");ylabel("y");

% Optional durable evidence; this serializes records and never executes on load:
% saveanalysis("cycle-verification.json",evidence);
