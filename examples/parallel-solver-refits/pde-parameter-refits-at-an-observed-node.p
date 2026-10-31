clc;clear;
native function real reaction(real u,real t,real x,real rate){return -rate*u;}
vector x=linspace(0,1,5);vector times=[0,0.05,0.1,0.2];
matrix initial=2*ones(1,5);matrix observed=(2*exp(-0.7*times))';
record fitted=pdemultistartfit(reaction,[0.2;0.5;1.1;1.6],times,observed,
    options(initial=initial,x=x,parameters=options(rate=0.3),fit_parameters="rate",
            observation_coordinates=[3],threads=4,
            solver=options(method="fd",boundary="neumann",diffusion=0.1,
                           time_step=0.005,sample_step=0.05)));
assert(fitted.successful==4);
display("Fitted PDE reaction rates:",fitted.parameters);
display("Derivative method:",fitted.jacobian);
% Each complete native PDE fit owns its native solver state. Outer parallel
% refits use inner_threads=1 and finite differences of native PDE trajectories.
