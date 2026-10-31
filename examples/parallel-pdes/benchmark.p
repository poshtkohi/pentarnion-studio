% Controlled same-process comparison. Timings are observations on this machine,
% not portable speedup guarantees. Native compilation is excluded by tic/toc.
use_algebra("A_gamma",gamma=1);clc;clear;
integer nodes=4096;
matrix x=(0:nodes-1)/nodes;
pentarnion direction=0.1+0.2*i-0.1*j+0.05*k+0.15*ell;
matrix initial=(1+0.2*sin(2*acos(-1)*x))*direction;
native function pentarnion reaction(pentarnion p,real t,real x,
    pentarnion A,pentarnion B) {return A*p+p*(B*p);}
pentarnion A=-0.2+0.1*i+0.1*j;
pentarnion B=0.01+0.02*k+0.01*ell;
record settings=options(parameters=options(A=A,B=B),boundary="periodic",
    length=1.0,diffusion=0.000001,t1=0.002,time_step=0.00002,
    sample_step=0.002,max_native_operations=1000000000,threads=1);
matrix times=zeros(3,3);
for(repetition=1:3) {
    tic;record serial=pdesolve(reaction,x,initial,with_options(settings,threads=1));
    times(repetition,1)=toc();
    tic;record four=pdesolve(reaction,x,initial,with_options(settings,threads=4));
    times(repetition,2)=toc();
    tic;record eight=pdesolve(reaction,x,initial,with_options(settings,threads=8));
    times(repetition,3)=toc();
    assert(serial.values==four.values);
    assert(serial.values==eight.values);
    assert(four.threads_used<=4&&eight.threads_used<=8);
    display("Actual workers for serial / four / eight:",
        serial.threads_used,four.threads_used,eight.threads_used);
}
display("Elapsed seconds: three repetitions, columns 1 / 4 / 8 workers:",times);
display("All five coefficients of every retained spatial value agree exactly.");
