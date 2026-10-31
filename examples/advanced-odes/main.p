use_algebra("A_gamma", gamma=1);
clc;
clear;
close("all");

% Events, directional crossing and backward integration use native callbacks.
ode decay(t,real y;real rate) { y'=-rate*y; }
native function real half_height(real time,real y,real rate) {
    return y-0.5;
}
solution stopped=odesolve(decay,y=1.0,rate=0.7,t0=0,t1=4,
    sample_step=0.4,method="bdf",rtol=1e-10,atol=1e-12,
    events=half_height,event_direction=-1,event_terminal=1);
record crossings=odeevents(stopped);
assert(abs(crossings.t(1)-log(2)/0.7)<1e-7);
display("Terminal half-height event; expected log(2)/0.7:",crossings);

% Native CVODES integrates S'=J_x*S+J_theta, not only an instantaneous J_x.
solution s=odesolve(decay,y=1.0,rate=0.7,t0=0,t1=4,sample_step=0.4,
    method="bdf",rtol=1e-10,atol=1e-12,sensitivities="all");
matrix rate_sensitivity=odesens(s,1);
matrix initial_sensitivity=odesens(s,2);
assert(abs(rate_sensitivity(end)+4*exp(-2.8))<1e-7);
assert(abs(initial_sensitivity(end)-exp(-2.8))<1e-7);
record statistics=odestats(s);
display("Integrated parameter and initial-state sensitivities:",rate_sensitivity,initial_sensitivity);
display("Native solver statistics:",statistics);

% deval evaluates stored integrator polynomials, independent of coarse output spacing.
vector fine_times=linspace(0,4,401);
vector dense_values=deval(s,fine_times);
assert(abs(deval(s,0.137)-exp(-0.7*0.137))<1e-7);
solution retained=odewindow(s,1.0);
display("Transient excluded; first retained sample is1.2:",retained);

% Reverse the interval while keeping sample_step a positive magnitude.
solution reverse=odesolve(decay,y=exp(-2.8),rate=0.7,t0=4,t1=0,
    sample_step=0.4,method="adams",rtol=1e-10,atol=1e-12);
assert(abs(reverse.y(end)-1)<1e-7);

figure(1,"Name","Native ODE events and sensitivities","NumberTitle","off");
subplot(2,2,1);plot(fine_times,dense_values);title("Native dense output");xlabel("$t$");ylabel("$y(t)$");
subplot(2,2,2);plot(s.t,rate_sensitivity);title("Parameter sensitivity $\\partial y/\\partial r$");xlabel("$t$");
subplot(2,2,3);plot(s.t,initial_sensitivity);title("Initial-state sensitivity $\\partial y/\\partial y_0$");xlabel("$t$");
subplot(2,2,4);plot(reverse.t,reverse.y);title("Backward integration");xlabel("$t$");
