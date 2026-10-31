% Native independent PDE runs share the process CPU budget.
use_algebra("A_gamma", gamma=1);
native function real growth(real u,real t,real x,real rate) { return rate*u*(1-u); }
real pi=3.141592653589793;
matrix x=linspace(0,1,33);
matrix initial=.2+.05*cos(2*pi*x);
record batch=pdeensemble(growth,x,initial,options(scan=options(rate=[.5,1,2,3]),boundary="neumann",integrator="imex_euler",diffusion=.02,t1=2,time_step=.005,sample_step=.05,threads=4,retain=[1,4]));
display("Run statuses: ",batch.task_status);
display("Final spatial means: ",batch.final_mean);
display("Requested/granted/used threads: ",batch.threads_requested,batch.threads_granted,batch.threads_used);
record slow=pderun(batch,1);
record fast=pderun(batch,4);
figure(1,"Name","Parallel reaction-rate scan","NumberTitle","off");
subplot(1,2,1); plot(slow.t,slow.mean); title("Rate = 0.5"); xlabel("$t$"); ylabel("Spatial mean");
subplot(1,2,2); plot(fast.t,fast.mean); title("Rate = 3"); xlabel("$t$"); ylabel("Spatial mean");
% Explicit replay verifies the cached reaction structure and uses saved settings.
record replay=pdereplay(growth,batch,4);
assert(norm(replay.values-fast.values,"fro")<1e-12);
