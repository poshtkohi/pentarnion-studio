% Equivalent sections share one crossing time, so native AD derivatives exist.
clc;
close("all");

ode drift(t, real x; real speed, real target) {
    x' = speed;
}

native function matrix<2,1> sections(real t, real x, real speed, real target) {
    return [x-target; 2*(x-target)];
}

native function real reset(real event_index, real t, real x,
                           real speed, real target) {
    if (event_index == 1) {
        return 0.0;
    } else {
        return x-2.0;
    }
}

record priority = hybridsolve(drift, sections, reset, 0.0,
    options(parameters=options(speed=1.0, target=1.0), t1=1.2,
            sample_step=.01, event_direction=[1,1], event_priority=[5,1]));
record composed = hybridsolve(drift, sections, reset, 0.0,
    options(parameters=options(speed=1.0, target=1.0), t1=1.2,
            sample_step=.01, event_direction=[1,1], event_priority=[0,1],
            simultaneous="all"));

display("Priority winner (expected 2): ", priority.event_info.indices);
display("Priority final value (expected -0.8): ", priority.values(end));
display("Composed final value (expected -1.8): ", composed.values(end));
display("Final derivatives w.r.t. speed,target,x0 (expected [1.2,-1,1]): ",
        composed.sensitivities(end,:));
display("Unchanged callbacks reused: ", composed.cache_hit);

figure(1, "Name", "Native simultaneous sections", "NumberTitle", "off");
subplot(1,2,1);
plot(priority.t, priority.values);
title("Priority: component 2"); xlabel("$t$"); ylabel("$x$");
subplot(1,2,2);
plot(composed.t, composed.values);
title("All: reset 1 then reset 2"); xlabel("$t$"); ylabel("$x$");
% The vertical pre/post segment denotes the instantaneous reset explicitly.
