% Same source/data/settings: completed fits are reused; interrupted fits restart.
use_algebra("A_gamma",gamma=1);clc;clear;
rng(42);
matrix x=linspace(0,10,41);
matrix y=2+3*x+.1*randn(1,41);
native function real line(real intercept,real slope,real x){return intercept+slope*x;}
record draws=fitbootstrap(line,[0,0],x,y,options(samples=32,sample_method="residual",seed=42,threads=4,checkpoint=".penta-cache/refits/bootstrap.json",resume=1,retry_failed=1));
display("Restored completed tasks / final successful fits:",draws.restored,draws.successful);
