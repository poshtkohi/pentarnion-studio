use_algebra("A_gamma",gamma=1);
clc;clear;
native function real reaction(real u,real t,real x,real source) {
    return source;
}
% Coordinate1 is source; coordinate2 is the appended solver diffusion.
% A spatially constant field has no diffusion contribution to its spatial mean.
record effects=modelsensitivity(@reaction,[0,.05],[1,.15],
    options(kind="pde1",initial=zeros(1,8),x=0:.125:.875,
        parameters=options(source=0.0),summary="spatial_mean",
        method="morris",samples=4,threads=4,
        solver=options(t1=.1,time_step=.01,sample_step=.1,diffusion=.1)));
display("Expected source/diffusion mean effects [.1,0]: ",effects.mu_star);
display("Solver parameter layout: ",effects.parameter_layout);
% Try solver method="fem" for the consistent-mass1D FE reference.
