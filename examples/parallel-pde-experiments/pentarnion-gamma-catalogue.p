% Every trajectory retains all five coefficients and its own immutable gamma.
use_algebra("A_gamma", gamma=1);
native function pentarnion reaction(pentarnion u,real t,real x,pentarnion A) { return A*u-.1*(u*u); }
real pi=3.141592653589793;
matrix x=linspace(0,1,17);
vector initial=(.1+.02*cos(2*pi*x))*(1+.1*i+.05*ell);
record batch=pdeensemble(reaction,x,initial,options(parameters=options(A=-.2+.1*j),gammas=[.2,.6,1],boundary="neumann",diffusion=.01,t1=.5,time_step=.001,sample_step=.025,threads=3,retain=[1,3]));
display("Gamma catalogue: ",batch.gammas);
display("Final coefficient means [1,i,j,k,ell]: ",batch.final_mean);
record field=pderun(batch,1);
% Raw node_coefficients records contain plain real columns; no coefficient is relabelled with workspace gamma.
matrix scalar_field=pdecoeff(field,0);
figure(1,"Name","Gamma 0.2 field","NumberTitle","off");
plot(field.x,scalar_field(end,:)); xlabel("$x$"); ylabel("Scalar coefficient");
figure(2,"Name","Saved field animation","NumberTitle","off");
animatefield(field,options(component=1,stride=2,max_frames=40));
% pdeparams returns a Pentarnion parameter as five real coefficients.
display("Raw A coefficients: ",pdeparams(batch,1));
% Typed pdesettings/pdereplay require selected gamma == current workspace gamma.
record replay=pdereplay(reaction,batch,3);
assert(replay.gamma==1);
