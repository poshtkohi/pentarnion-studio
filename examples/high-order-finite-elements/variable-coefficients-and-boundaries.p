use_algebra("A_gamma",gamma=1);
clc;clear;close("all");
% -div((1+x) grad u)+2*u=f, exact u=x+y.
% All callbacks are real native cached functions; no interpreter per quadrature.
native function real diffusion(real x,real y){return 1+x;}
native function real forcing(real x,real y){return -1+2*(x+y);}
native function real boundary(real x,real y){return x+y;}
record M=trimesh([0,0;1,0;1,1;0,1;0.43,0.56],
    [1,2,5;2,3,5;3,4,5;4,1,5],
    options(boundary_tags=[1,2,1;2,3,2;3,4,3;4,1,4]));
record s=femsolve(M,@forcing,options(order=2,diffusion=@diffusion,
    reaction=2,dirichlet=@boundary,exact=@boundary));
display("Converged / L2 error: ",s.converged,s.l2_error);
display("Minimum sampled diffusion: ",s.min_sampled_diffusion);
display("CPU policy: ",s.thread_policy);
figure(1,"Name","Variable-coefficient elliptic PDE","NumberTitle","off");
femplot(s);title("Variable diffusion with native nonzero Dirichlet data");
