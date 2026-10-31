use_algebra("A_gamma", gamma=1);
clc;
clear;
matrix observations=[1,2,3,4,5,6,7,8];
assert(variance(observations)==6);
matrix quartiles=quantile(observations,[0,0.25,0.5,0.75,1]);
record boot=bootstrap(observations,options(replicates=500,seed=2026,
    statistic="mean",confidence=0.95));
record bins=histogram(boot.estimates,linspace(1,8,15));
matrix pair_covariance=covariance([1,2;2,4;3,6]);
display("Sample variance: ",variance(observations));
display("Quartiles(type7): ",quartiles);
display("IID percentile interval for the sample mean: ",boot.lower,boot.upper);
display("Covariance: ",pair_covariance);
figure(17,"Name","Statistics and uncertainty","NumberTitle","off");
matrix replicates=1:500;
subplot(1,2,1);plot(replicates,boot.estimates);title("Seeded bootstrap mean estimates");
subplot(1,2,2);plot(bins.centers,bins.density);title("Bootstrap histogram density");
% IID resampling is a statistical assumption, not a general time-series method.
