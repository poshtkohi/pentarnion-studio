% One PDE solve uses Studio's shared native CPU worker pool.
use_algebra("A_gamma",gamma=1);clc;clear;
real pi=3.141592653589793;
matrix x=(0:255)/256;
matrix initial=(1+i-j+.25*ell)+.05*cos(2*pi*x)*j;
native function pentarnion reaction(pentarnion p,real t,real x,real rate){
    return -rate*p+.01*sin(x+t)*ell;
}
tic;
record serial=pdesolve(reaction,x,initial,options(parameters=options(rate=.2),length=1,diffusion=.01,t1=.02,time_step=.0001,sample_step=.01,threads=1));
real serial_seconds=toc();
tic;
record parallel=pdesolve(reaction,x,initial,options(parameters=options(rate=.2),length=1,diffusion=.01,t1=.02,time_step=.0001,sample_step=.01,threads=4));
real parallel_seconds=toc();
for(row=1:size(serial.values,1)){
    for(column=1:size(serial.values,2)){
        assert(norm2(serial.values(row,column)-parallel.values(row,column))==0);
    }
}
display("Serial/parallel elapsed seconds (native compilation excluded):",serial_seconds,parallel_seconds);
display("Requested / granted / actually used:",parallel.threads_requested,parallel.threads_granted,parallel.threads_used);
display("Parallel kernel dispatches:",parallel.parallel_calls);
% Small solves can cost more in dispatch overhead; no speedup is promised.
