use_algebra("A_gamma",gamma=1);
clc;clear;
ode weak_growth(t,real x,real y) {
    x'=.005*x;
    y'=-y;
}
record screen=discover(weak_growth,[1,1],options(t1=1.0,sample_step=.05));
record evidence=verifyexperiment(weak_growth,screen,[1],
    options(horizons=[4,8],lyapunov=1,qr_step=.25,
        minimum_observation=1.0,minimum_positive=.02));
display("Positive .005 exponent remains inconclusive: ",
    evidence.largest_lyapunov,evidence.labels);

% Independent oscillators with incommensurate frequencies1 and sqrt(2).
% Near-zero finite-time growth alone does not certify quasiperiodicity.
ode two_frequencies(t,real x,real vx,real y,real vy) {
    x'=vx;vx'=-x;
    y'=vy;vy'=-2*y;
}
record neutral_screen=discover(two_frequencies,[1,0,1,0],options(t1=1.0));
record neutral=verifyexperiment(two_frequencies,neutral_screen,[1],
    options(horizons=[20,40],lyapunov=1,qr_step=.25,
        minimum_observation=10.0,lyapunov_tolerance=.1));
display("Neutral finite-time spectrum: ",neutral.exponents);
display("Conservative labels: ",neutral.labels);
