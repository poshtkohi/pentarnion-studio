use_algebra("A_gamma",gamma=1);
clc;clear;
native function real affine_step(real state,real a,real b) {
    return state+a+2*b;
}
record effects=modelsensitivity(@affine_step,[0,0],[1,1],
    options(kind="map",initial=[0],parameters=options(a=0.0,b=0.0),
        method="morris",samples=8,threads=4,solver=options(iterations=5)));
display("Five native iterations; expected effects [5,10]: ",effects.mu_star);
display("Fixed/scanned runtime parameter layout: ",effects.parameter_layout);
