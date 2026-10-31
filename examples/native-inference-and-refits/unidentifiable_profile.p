% The model depends only on a+b: fitting cannot identify the two separately.
use_algebra("A_gamma",gamma=1);clc;clear;close("all");
matrix x=0:9;matrix y=5+.01*sin(x);
native function real response(real a,real b,real x){return a+b;}
record profile=profilefit(response,[1,1],x,y,1,linspace(0,5,21),options(threads=4));
display("Two unknowns, baseline Jacobian rank:",profile.base_rank);
figure(54,"Name","A deliberately unidentifiable parameter pair","NumberTitle","off");
plot(profile.grid,profile.delta_sse);title("Flat profile after refitting the compensating parameter");xlabel("a");ylabel("Difference in SSE");
record draws=fitbootstrap(response,[1,1],x,y,options(samples=16,threads=4,seed=42));
display("Rank-deficient refits / valid parameter bands:",draws.rank_deficient,draws.statistics_valid);
assert(draws.statistics_valid==0);
