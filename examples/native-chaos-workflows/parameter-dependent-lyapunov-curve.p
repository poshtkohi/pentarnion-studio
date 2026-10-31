% Independent analytic check: the only exponent of x'=-rate*x is -rate.
ode decay(t, real x; real rate) { x' = -rate*x; }
record scan = lyapunovscan(decay, [1], options(
    parameters=options(rate=searchset([0.25,0.5,1,2])), threads=4,
    t1=0.5, sample_step=0.05, horizons=[1,2], minimum_observation=0.2,
    qr_step=0.25, verification_checkpoint="decay-verification.json",
    verification_overwrite=1));
record evidence = scan.verification;
display("Final-tier exponents; exact reference = -rate: ", evidence.exponents(5:8,1));
figure(1, "Name", "Native parameter-dependent Lyapunov curve");
plot(scan.parameters(:,1), evidence.exponents(5:8,1));
xlabel("rate"); ylabel("Largest finite-time Lyapunov exponent");
caption("Complete ordered spectra and convergence flags are retained in scan.verification.");
