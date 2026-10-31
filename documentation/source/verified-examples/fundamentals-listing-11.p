use_algebra("A_gamma",gamma=1);
pentarnion A = -1/2 - 2*i + j/2 - k - ell/2;
pentarnion B = 1 + 2*j - ell/2;
pentarnion p0 = (1-i-j+k-ell)/2;
real eta = 0;
ode bracketing(t,pentarnion p;pentarnion A,pentarnion B,real eta) {
    p' = A*p + (1-eta)*(B*(p*p)) + eta*((B*p)*p);
}
solution s = odesolve(bracketing,p=p0,A=A,B=B,eta=eta,
    t0=0,t1=200,sample_step=0.01,
    method="bdf",jacobian="analytic",rtol=1e-10,atol=1e-12);
