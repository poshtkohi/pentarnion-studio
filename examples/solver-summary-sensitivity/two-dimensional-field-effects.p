use_algebra("A_gamma",gamma=1);
clc;clear;
native function real reaction(real u,real t,real x,real y,real source) {
    return source;
}
record effects=modelsensitivity(@reaction,[0,.05,.05],[1,.15,.15],
    options(kind="pde2",initial=zeros(1,16),x=0:.25:.75,y=0:.25:.75,
        parameters=options(source=0.0),summary="spatial_mean",
        method="morris",samples=4,threads=4,
        solver=options(t1=.1,time_step=.01,sample_step=.1)));
display("Expected source/diffusion-x/diffusion-y effects [.1,0,0]: ",effects.mu_star);
display("Retained2D spatial axes: ",effects.x,effects.y);
