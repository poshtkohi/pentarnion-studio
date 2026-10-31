% Synthetic IID Gaussian observations have explicitly chosen variance .01.
use_algebra("A_gamma",gamma=1);clc;clear;
rng(42);
matrix x=linspace(0,10,41);
matrix y=2+3*x+.1*randn(1,41);
native function real line(real intercept,real slope,real x){return intercept+slope*x;}
close("all");
matrix grid=linspace(2.9,3.1,21);
record profile=profilefit(line,[0,0],x,y,2,grid,options(objective="gaussian_known_variance",noise_variance=.01,threads=4));
display("Local fitted reference parameters / rank:",profile.base_parameters,profile.base_rank);
display("Profile slopes / refitted nuisance intercepts:",profile.grid,profile.parameters(:,1));
display("Any profile point improved the local baseline:",profile.baseline_improved);
figure(52,"Name","Profile of an explicitly assumed Gaussian likelihood","NumberTitle","off");
plot(profile.grid,profile.deviance);title("Profile deviance: nuisance intercept refitted");xlabel("Slope");ylabel("$\\Delta\\mathrm{SSE}/\\sigma^2$");
% No automatic Wilks threshold or parameter-uniqueness conclusion is asserted.
