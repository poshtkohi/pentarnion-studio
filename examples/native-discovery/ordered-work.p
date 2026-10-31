use_algebra("A_gamma",gamma=1);
clc;clear;close("all");
% q,v are physical states. W,V,C are auxiliary integrals, initially zero.
ode work(t,pentarnion q,pentarnion v,pentarnion W,pentarnion V,pentarnion C;
    pentarnion A,pentarnion B,real m) {
    q'=v;
    v'=(A*q+B*(q*q))/m;
    W'=(A*q+B*(q*q))*v;
    V'=v*(A*q+B*(q*q));
    C'=(A*q+B*(q*q))*v-v*(A*q+B*(q*q));
}
pentarnion q0=pent([0.1,0.2,-0.1,0.05,0.15]);
pentarnion v0=pent([0.2,-0.1,0.25,0.1,-0.05]);
pentarnion A=pent([0.1,0.2,-0.1,0,0.05]);
pentarnion B=pent([0.03,-0.1,0.2,-0.05,0.1]);
real m=1.7;
solution s=odesolve(work,q=q0,v=v0,W=0,V=0,C=0,A=A,B=B,m=m,
    t0=0,t1=0.2,sample_step=0.001,method="adams",rtol=1e-11,atol=1e-13);
% W+V=m*(v*v-v0*v0); W=(m*(v*v-v0*v0)+C)/2.
% C is the integrated commutator and cannot generally be omitted.
figure(1,"Name","Ordered Pentarnion work","NumberTitle","off");
for(component=0:4) {
    subplot(3,2,component+1);
    plot(s.t,coeff(s.W,component));
    title("Ordered work coefficient");xlabel("$t$");
}
display("Ordered work has five components; all integrals begin at zero.");
