% Attached first PDE report, equations (6), (13), (14).
% Change eta between0 and1. Runtime parameter changes reuse the native body.
use_algebra("A_gamma",gamma=1);
clc; clear;
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
