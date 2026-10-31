use_algebra("A_gamma",gamma=1);
clc; clear; close("all");

% A general native ODE ensemble, independent of the force-law benchmark.
ode oscillator(t,real x,real v;real frequency) {
    x'=v;
    v'=-frequency*x;
}
matrix initials=[1,0;2,0;0,1];
record frequencies=searchinterval(1,4,options(samples=3,method="linspace"));
record settings=options(parameters=options(frequency=frequencies),
    threads=4,t0=0,t1=20,sample_step=0.02,method="adams",
    rtol=1e-10,atol=1e-12,transient=2.0,min_return_time=1.0,
    return_tolerance=0.02,max_runs=32,max_native_operations=10000000);
record plan=discoverplan(oscillator,initials,settings);
display("Preflight candidates / estimated peak bytes:",plan.runs,plan.estimated_bytes);
tic;
record experiment=odeensemble(oscillator,initials,settings);
toc;
display("Completed / actual workers:",experiment.completed,experiment.threads_used);
display("Run 1 finite-time evidence:",discoverstatus(experiment,1));

% Replay only one selected run, using the same compiled native model.
record selected=experimentrun(oscillator,experiment,1,options(rtol=1e-11,atol=1e-13));
figure(1,"Name","Native oscillator experiment","NumberTitle","off");
subplot(1,2,1);plot(selected.t,selected.values(:,1));
title("Selected run: x(t)");xlabel("$t$");ylabel("$x$");
subplot(1,2,2);plot(selected.values(:,1),selected.values(:,2));
title("Complete two-coordinate phase portrait");xlabel("$x$");ylabel("$v$");

% Optional durable execution: add checkpoint="oscillators.json" to settings.
% Later set resume=1 to skip validated completed tasks. Keep the old file if
% source structure, candidate inputs or scientific settings have changed.
