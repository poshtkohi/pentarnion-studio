threads(1);
ode decay(t,real x;real rate) { x' = -rate*x; }
solution s = odesolve(decay,x=1.0,rate=0.5,
    t0=0,t1=2,sample_step=0.05,
    method="bdf",jacobian="analytic",rtol=1e-9,atol=1e-11);
real difference = abs(deval(s,2)-exp(-1));
assert(difference < 1e-8);
display("Decay endpoint absolute error:",difference);
display(s);
