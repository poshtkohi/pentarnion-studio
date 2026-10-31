native function real heat(real u,real t,real x) {
    return 0.0;
}
native function real initial_profile(real t,real x) {
    real pi=3.14159265358979323846;
    return cos(pi*x);
}
record report=pdeconverge(heat,linspace(0,1,9),initial_profile,options(
    diffusion=.1,t1=.2,time_step=.005,sample_step=.02,adaptive=0,
    spatial_levels=4,spatial_rtol=.002,spatial_atol=1e-6));
display("Spatial convergence status: ",report.status);
display("Levels / final nodes: ",report.levels,report.field.nodes);
display("Scaled inter-grid difference: ",report.scaled_difference);
display("History [level,nodes,work,seconds,has_estimate,scaled,absolute]: ",report.history);
matrix U=pdeimplicitcoeff(report.field,0);
figure(1,"Name","Spatial convergence workflow","NumberTitle","off");
subplot(1,2,1);plot(report.field.x,U(end,:));title("Finest retained heat profile");
subplot(1,2,2);plot(report.history(:,2),report.history(:,6));
xlabel("nodes");ylabel("scaled difference");
title("Successive uniform refinements; finite-grid estimates");
