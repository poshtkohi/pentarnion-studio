use_algebra("A_gamma",gamma=1);
clc;clear;close("all");
% Unit-circle base cycle with transverse half-rotation coordinates.
% Multipliers:1,exp(-4*pi),-exp(2*pi*mu),-exp(-2*pi).
% A simple flip crossing occurs at mu=0; cubic criticality is not inferred.
ode half_rotation(t,real x,real y,real u,real v;real mu) {
    x'=-y+(1-x*x-y*y)*x;
    y'=x+(1-x*x-y*y)*y;
    u'=((mu-1)/2+(mu+1)*x/2)*u+((mu+1)*y/2-.5)*v;
    v'=((mu+1)*y/2+.5)*u+((mu-1)/2-(mu+1)*x/2)*v;
}
tic;
record point=refinebifurcation(half_rotation,[1,0,0,0],
    options(kind="period_doubling",parameter="mu",parameters=options(mu=.01),
        period=6.2,rtol=1e-10,atol=1e-12,tolerance=1e-7,
        fd_agreement=.01,max_native_operations=100000000));
toc;
display("Refined mu / period, expected0 /2*pi: ",point.parameter,point.period);
display("Critical real multiplier / neutral error: ",point.critical_real,point.neutral_error);
display("Multiplier crossing derivative, expected-2*pi: ",point.crossing_derivative);
display("Centered-difference h/h2 disagreement: ",point.fd_step_difference);
display("Explicit derivative strategy: ",point.corrector_derivatives);
display("Numerical candidate: ",point.status,point.message);
figure(1,"Name","Native period-doubling refinement","NumberTitle","off");
subplot(1,2,1);plot(point.history(:,1),point.history(:,2));
title("Full closure/phase/flip augmented residual");xlabel("Newton iteration");ylabel("Residual");
subplot(1,2,2);plot([1,2,3,4],point.spectrum_real);
title("Real Floquet spectrum at the refined point");xlabel("Multiplier index");ylabel("Real part");
