% Two independent rotations with incommensurate analytic frequencies.
% The numerical classifier reports a candidate; it cannot prove irrationality.
ode rotations(t, real x, real y, real u, real v) {
    x'=-y; y'=x; u'=-sqrt(2)*v; v'=sqrt(2)*u;
}
record scan=lyapunovscan(rotations,[1,0,1,0],options(t1=20,
    sample_step=0.1,horizons=[40,80],qr_step=1,return_tolerance=0.4,
    min_return_time=1,minimum_observation=20,lyapunov_tolerance=0.01));
display(scan.verification.labels);
display("Near-zero exponent counts: ",scan.verification.near_zero_exponents);
display("Candidate flags: ",scan.verification.quasiperiodic_candidate);
display("Full ordered finite-time spectra: ",scan.verification.exponents);
record replay=verifyrun(rotations,scan.verification,2);
figure(1,"Name","Two rotations: finite-time evidence");
plot(replay.t,replay.values(:,1));
xlabel("t");ylabel("x(t)");
caption("A long-period/resonant orbit cannot be excluded from finite numerical evidence alone.");
