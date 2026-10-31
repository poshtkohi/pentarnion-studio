% Native headless AVI: no GUI, external process or codec dependency.
clc;clear;
real pi=3.141592653589793;
matrix x=linspace(0,1,33);
native function real no_reaction(real u,real t,real x){return 0.0;}
record field=pdesolve(no_reaction,x,sin(pi*x),
    options(method="fd",boundary="dirichlet",integrator="imex_euler",
        diffusion=.1,t1=.2,time_step=.001,sample_step=.02,threads=4));
record movie=exportvideo(field,"heat-playback.avi",
    options(first=1,last=11,stride=1,max_frames=11,fps=15.0,
        width=640,height=480,dpi=100,max_bytes=64000000,
        max_seconds=60,overwrite=1));
display("Native video codec / frames:",movie.codec,movie.frames);
display("Uncompressed AVI bytes:",movie.bytes);
display("Movie display duration:",movie.duration_seconds);
display("Original physical sample duration:",movie.source_duration_seconds);
display("Saved AVI:",movie.path);
