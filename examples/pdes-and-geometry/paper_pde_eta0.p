% Attached first PDE report, equations (6), (13), (14).
% Change eta between0 and1. Runtime parameter changes reuse the native body.
use_algebra("A_gamma",gamma=1);
clc; clear; close("all");
real pi=3.141592653589793;
real eta=0.0;
pentarnion A=-1/2-2*i+j/2-k-ell/2;
pentarnion B=1+2*j-ell/2;
pentarnion p0=(1-i-j+k-ell)/2;
real L=20;
matrix x=(0:127)*(L/128);
matrix initial=p0+.001*sin(2*pi*x/L)+.0007*cos(4*pi*x/L)*j+.0004*sin(6*pi*x/L+.3)*ell;
native function pentarnion reaction(pentarnion p,real t,real s,pentarnion A,pentarnion B,real eta){
    return A*p+(1-eta)*(B*(p*p))+eta*((B*p)*p);
}
tic;
record field=pdesolve(reaction,x,initial,options(method="fd",boundary="periodic",length=L,diffusion=.1,t0=0,t1=120,time_step=.005,sample_step=.05,parameters=options(A=A,B=B,eta=eta),max_steps=30000,max_reaction_evaluations=16000000,max_wall_seconds=180));
toc;
assert(abs(field.spatial_rms(1)-.0009082951062292429)<1e-13);
display("Bracketing eta:",eta);
display("Initial and final spatial RMS:",field.spatial_rms(1),field.spatial_rms(end));
display("Mean late RMS (100 <= t <=120):",mean(field.spatial_rms(2001:end)));
display("Native steps / reaction evaluations / work:",field.steps,field.reaction_evaluations,field.native_operations);
% Figures use every twentieth stored time for a clear and bounded display.
figure(15,"Name","First Pentarnion PDE: component fields","NumberTitle","off");
subplot(3,2,1);
matrix component_0=coeff(field.values(1:20:end,:),0);
imagesc(field.x,field.t(1:20:end),component_0);
title("scalar component");xlabel("Space");ylabel("Time");
subplot(3,2,2);
matrix component_1=coeff(field.values(1:20:end,:),1);
imagesc(field.x,field.t(1:20:end),component_1);
title("i component");xlabel("Space");ylabel("Time");
subplot(3,2,3);
matrix component_2=coeff(field.values(1:20:end,:),2);
imagesc(field.x,field.t(1:20:end),component_2);
title("j component");xlabel("Space");ylabel("Time");
subplot(3,2,4);
matrix component_3=coeff(field.values(1:20:end,:),3);
imagesc(field.x,field.t(1:20:end),component_3);
title("k component");xlabel("Space");ylabel("Time");
subplot(3,2,5);
matrix component_4=coeff(field.values(1:20:end,:),4);
imagesc(field.x,field.t(1:20:end),component_4);
title("ell component");xlabel("Space");ylabel("Time");
subplot(3,2,6);plot(field.t,field.spatial_rms);title("Spatial RMS deviation");xlabel("Time");ylabel("RMS");
figure(16,"Name","PDE scalar signal at four spatial positions","NumberTitle","off");
subplot(2,2,1);matrix signal_0=coeff(field.values(:,1),0);plot(field.t,signal_0);title("Scalar signal at s=0");xlabel("Time");ylabel("Scalar component");
subplot(2,2,2);matrix signal_1=coeff(field.values(:,33),0);plot(field.t,signal_1);title("Scalar signal at s=5");xlabel("Time");ylabel("Scalar component");
subplot(2,2,3);matrix signal_2=coeff(field.values(:,65),0);plot(field.t,signal_2);title("Scalar signal at s=10");xlabel("Time");ylabel("Scalar component");
subplot(2,2,4);matrix signal_3=coeff(field.values(:,97),0);plot(field.t,signal_3);title("Scalar signal at s=15");xlabel("Time");ylabel("Scalar component");
tilefigures("studio",1,2);
