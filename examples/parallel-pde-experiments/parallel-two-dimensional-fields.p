native function real logistic(real u,real t,real x,real y,real rate) { return rate*u*(1-u); }
matrix x=linspace(0,1,17); matrix y=linspace(0,1,17);
matrix initial=.15*ones(17,17);
record batch=pdeensemble2(logistic,x,y,initial,options(scan=options(rate=[.5,1,2]),boundary="neumann",integrator="imex_euler",diffusion=.02,t1=1,time_step=.01,sample_step=.05,threads=3,retain=[1,3]));
record field=pderun(batch,3);
matrix last=pdeslice(field,size(field.t,1));
display("Selected final field shape: ",size(last));
figure(1,"Name","Parallel 2D reaction fields","NumberTitle","off");
animatefield(field,options(mode="surface",max_frames=40));
