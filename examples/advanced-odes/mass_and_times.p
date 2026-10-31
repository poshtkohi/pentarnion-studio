use_algebra("A_gamma",gamma=1);
clc;
clear;
ode mass_decay(t,real y;real rate){y'=-rate*y;}
% Explicit real-coordinate constant mass M*y'=f; singular DAEs are rejected.
solution s=odesolve(mass_decay,y=2.0,rate=0.7,t0=0,t1=2,
    output_times=[0,0.13,0.47,1.23,2],mass=[2.0],method="bdf",
    rtol=1e-10,atol=1e-12,max_step=0.1,max_order=4);
assert(abs(s.y(end)-2*exp(-0.7))<1e-7);
display("Arbitrary exact requested output times:",s.t);
display("Mass-matrix decay values:",s.y);
display(odestats(s));
