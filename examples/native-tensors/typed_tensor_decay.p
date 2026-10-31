% Shape-specialized native tensor ODE, cached across runtime-value changes.
use_algebra("A_gamma",gamma=1);
clc;
clear;
close("all");
ode decay(t,tensor<2,2,2> x;real rate) {
    x' = -rate*x;
}
tensor<2,2,2> initial = tensor([1,2,3,4,5,6,7,8],[2,2,2]);
tic;
solution simulation = odesolve(decay,x=initial,rate=0.5,t0=0,t1=5,
    sample_step=0.02,method="bdf",jacobian="analytic",rtol=1e-10,atol=1e-12);
toc;
tensor<2,2,2> final = simulation.x(length(simulation.t));
display("Final tensor and analytic error:",final,norm(final-exp(-2.5)*initial));
figure(1,"Name","Tensor ODE coordinates","NumberTitle","off");
subplot(2,1,1);
plot(simulation.t,statecoord(simulation.x,0));
title("Tensor entry $(1,1,1)$");xlabel("$t$");ylabel("$x_{111}$");
subplot(2,1,2);
plot(simulation.t,statecoord(simulation.x,7));
title("Tensor entry $(2,2,2)$");xlabel("$t$");ylabel("$x_{222}$");
