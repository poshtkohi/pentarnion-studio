% Same callback, inputs and scientific settings; scheduling may change.
native function real decay(real u,real t,real x,real rate) { return -rate*u; }
matrix x=linspace(0,1,33);
record batch=pdeensemble(decay,x,ones(1,33),options(scan=options(rate=[.2,.4,.6,.8,1,1.2]),boundary="neumann",integrator="imex_euler",t1=1,time_step=.002,sample_step=.1,threads=2,batch_size=3,checkpoint="parallel-pde-summary.json",resume=1,retry_failed=1));
display("Previously completed runs restored: ",batch.restored);
display(batch.final_mean);
