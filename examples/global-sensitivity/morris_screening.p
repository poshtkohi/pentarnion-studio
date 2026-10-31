% Morris effects are derivatives over normalized input intervals, not physical units.
use_algebra("A_gamma",gamma=1);clc;clear;
native function real response(real x,real y,real scale){return scale*(x+2*y);}
record screen=sensitivity(response,[-1,0],[2,4],options(parameters=options(scale=1.0),method="morris",samples=128,levels=6,seed=42,threads=4));
display("Morris signed mean / absolute mean / dispersion:",screen.mu,screen.mu_star,screen.sigma);
assert(norm(screen.mu-[3,8],2)<1e-10);
display("Requested / granted / used workers:",screen.threads_requested,screen.threads_granted,screen.threads_used);
