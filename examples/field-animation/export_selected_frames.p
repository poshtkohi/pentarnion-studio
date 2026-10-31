% Headless-capable: explicitly export three selected saved frames, without GUI.
clc;clear;
real pi=3.141592653589793;
matrix x=linspace(0,1,33);
native function real no_reaction(real u,real t,real x){return 0.0;}
record field=pdesolve(no_reaction,x,sin(pi*x),
    options(method="fd",boundary="dirichlet",integrator="imex_euler",
        diffusion=.1,t1=.1,time_step=.001,sample_step=.01,threads=4));
record images=exportframes(field,"heat-selected-frames",
    options(prefix="heat",first=1,last=11,stride=5,max_frames=3,
        width=480,height=320,dpi=100,max_bytes=8000000,
        create_directory=1,overwrite=1));
display("Exported selected PNG frames:",images.frames);
display("Actual PNG bytes:",images.bytes);
display("Source-index/time/coefficient manifest:",images.manifest);
