% Pairs bootstrap assumes IID observation pairs. Bands describe the fitted mean.
use_algebra("A_gamma",gamma=1);clc;clear;
rng(42);
matrix x=linspace(0,10,41);
matrix y=2+3*x+.1*randn(1,41);
native function real line(real intercept,real slope,real x){return intercept+slope*x;}
close("all");
matrix prediction=linspace(0,10,61);
record draws=fitbootstrap(line,[0,0],x,y,options(samples=64,sample_method="pairs",prediction_x=prediction,confidence=.95,seed=2026,threads=4));
display("Successful / full-rank inference refits:",draws.successful,draws.statistics_refits);
display("Empirical parameter standard deviations and correlation:",draws.parameter_std,draws.correlation);
figure(53,"Name","Pointwise bootstrap fitted-mean bands","NumberTitle","off");
subplot(1,2,1);plot(draws.prediction_x,draws.fitted_lower);title("Lower pointwise fitted-mean percentile");xlabel("x");ylabel("Fitted mean");
subplot(1,2,2);plot(draws.prediction_x,draws.fitted_upper);title("Upper pointwise fitted-mean percentile");xlabel("x");ylabel("Fitted mean");
% These are not intervals for future noisy observations.
