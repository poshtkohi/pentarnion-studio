use_algebra("A_gamma", gamma=1);
clc;
clear;

ode decay(t, pentarnion p; real rate) {
    p' = -rate*p;
}

pentarnion p0 = 1+i+j+k+ell;
tic;
solution s = odesolve(decay,p=p0,rate=0.2,t0=0,t1=1,sample_step=0.1,
                     method="bdf",jacobian="analytic",rtol=1e-9,atol=1e-11);
real elapsed = toc();
display(s);
display("Elapsed execution seconds, excluding native compilation:",elapsed);
