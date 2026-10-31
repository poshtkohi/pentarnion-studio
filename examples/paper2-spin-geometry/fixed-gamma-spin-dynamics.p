% Original algebra-research example: fixed-gamma spin components and integrability.
% This is the paper's REAL Lie-Poisson cross product, not a canonical q,p model.
use_algebra("A_gamma",gamma=1);
clc;clear;close("all");
real algebra_gamma=1.0;
real A=3*(algebra_gamma^4+2*algebra_gamma^2+3)/((1+algebra_gamma^2)*(3+algebra_gamma^2));
real B=(4*algebra_gamma^2+3)/(1+algebra_gamma^2);
real g1=1/sqrt(3+algebra_gamma^2);
real g2=1/sqrt(2);real g3=1/sqrt(6);
ode spin(t,real yh,real yb,real yc;real A,real B,real g1,real g2,real g3){
    yh'=-g3*yb+g2*yc;
    yb'=(A-B)*yh*yc-g1*yc+g3*yh;
    yc'=(B-A)*yh*yb-g2*yh+g1*yb;
}
tic;
solution s=odesolve(spin,yh=1,yb=0,yc=0,A=A,B=B,g1=g1,g2=g2,g3=g3,
    t0=0,t1=12,sample_step=.02,method="adams",jacobian="analytic",rtol=1e-11,atol=1e-13);
toc;
% Materialize exact stored columns for array arithmetic; no interpolation.
matrix yh=reshape(s.yh,[],1);matrix yb=reshape(s.yb,[],1);matrix yc=reshape(s.yc,[],1);
matrix length_error=yh.^2+yb.^2+yc.^2-1;
matrix energy=(A*yh.^2+B*(yb.^2+yc.^2))/2-g1*yh-g2*yb-g3*yc;
matrix energy_error=energy-energy(1);
assert(norm(length_error,2)<1e-7&&norm(energy_error,2)<1e-7);
display("Sampled spin-length / energy error norms:",norm(length_error,2),norm(energy_error,2));
display("Fixed gamma spin motion preserves length and energy; no chaos claim.");
figure(2,"Name","Paper fixed-gamma spin","NumberTitle","off");
subplot(1,2,1);plot3(s.yh,s.yb,s.yc);xlabel("h_gamma");ylabel("b");zlabel("c");
title("Lie-Poisson spin trajectory in the orthonormal adapted basis");
subplot(1,2,2);plot(s.t,length_error);hold("on");plot(s.t,energy_error);hold("off");
legend("Length squared error","Energy error");xlabel("t");title("Numerical invariant errors");
