use_algebra("A_gamma",gamma=1);
clc;clear;
ode affine_flow(t,real x;real a,real b) {
    x'=a+2*b;
}
% Final x(1)=a+2*b. For independent uniform[0,1] inputs, the exact Sobol
% first/total effects are[0.2,0.8]. These small examples begin with exact Morris.
record effects=modelsensitivity(affine_flow,[0,0],[1,1],
    options(initial=[0],parameters=options(a=0.0,b=0.0),
        method="morris",samples=8,threads=4,
        solver=options(t1=1.0,sample_step=.5,rtol=1e-9,atol=1e-11)));
display("Exact Morris unit-interval effects [1,2]: ",effects.mu_star);
display("Actual shared workers: ",effects.threads_used);
display("Native body / scientific request hashes: ",effects.cache_key,effects.request_hash);

% Optional Monte Carlo experiment: change method to"sobol" and samples to1024.
% Use more samples/repeated seeds to assess Monte Carlo convergence.
