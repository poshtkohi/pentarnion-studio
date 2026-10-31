% Fully implicit nonlinear reaction; local analytic Jacobian is generated.
native function real cubic(real u, real t, real x, real rate) {
    return -rate*u*u*u;
}
matrix x=linspace(0,1,33);
tic;
record field=pdeimplicit(cubic,x,1.0,options(
    parameters=options(rate=1000.0),diffusion=0.0,
    t1=.02,time_step=.02,sample_step=.001,rtol=.002,atol=1e-8,threads=2));
toc;
matrix U=pdeimplicitcoeff(field,0);
display("Accepted/rejected steps: ",field.steps,field.rejected_steps);
display("Native Jacobian evaluations: ",field.jacobian_evaluations);
display("Last value / exact value: ",U(end,1),1/sqrt(41));
figure(1,"Name","Adaptive fully implicit stiff reaction","NumberTitle","off");
plot(field.t,U(:,1));
xlabel("time");ylabel("u");
title("$u_t=-1000u^3$, generated analytic Jacobian");
