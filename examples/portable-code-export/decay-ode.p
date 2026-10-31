use_algebra("A_gamma",gamma=1);
ode decay(t,real x,pentarnion p;real rate){
    x'=-rate*x;
    p'=-rate*p;
}
solution s=odesolve(decay,x=1,p=1+i+j+k+ell,rate=0.7,
    t0=0,t1=2,sample_step=0.05,method="bdf",jacobian="analytic",
    rtol=1e-10,atol=1e-12);
assert(abs(s.x(end)-exp(-1.4))<1e-8);
assert(norm2(s.p(end)-(1+i+j+k+ell)*exp(-1.4))<1e-16);
display("Known real and Pentarnion decay solutions passed.");
