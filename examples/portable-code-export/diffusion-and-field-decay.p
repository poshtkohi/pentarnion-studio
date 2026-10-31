native function real zero_reaction(real u,real t,real x) { return 0.0; }
matrix x=linspace(0,1,21);
matrix initial=sin(3.141592653589793*x);
record fd=pdesolve(zero_reaction,x,initial,options(method="fd",boundary="dirichlet",diffusion=.1,t1=.01,time_step=.0001,sample_step=.005));
record fem=pdesolve(zero_reaction,x,initial,options(method="fem",integrator="imex_euler",boundary="dirichlet",diffusion=.1,t1=.01,time_step=.0001,sample_step=.005));
display("Final FD and P1FEM fields:",fd.values(end,:),fem.values(end,:));
figure(1);plot(x,fd.values(end,:));title("Dirichlet heat equation: finite differences");
native function pentarnion pent_decay(pentarnion u,real t,real x) { return -u; }
pentarnionmatrix field=ones(1,21)*(1+2*i-j+.5*k+.25*ell);
record decay=pdesolve(pent_decay,x,field,options(method="fem",boundary="neumann",diffusion=.1,t1=.01,time_step=.0001));
matrix coefficient=pdecoeff(decay,1);
display("Whole-element mean after decay:",decay.mean(end));
figure(2);plot(x,coefficient(end,:));title("Complete-field decay: coefficient along i");
