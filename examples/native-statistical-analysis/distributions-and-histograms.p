record draws=distrand("student_t",1,4096,options(df=5,seed=42));
display("Recorded draw seed: ",draws.seed);
matrix quantiles=distinv("student_t",[0.025,0.5,0.975],options(df=5));
display("Student-t quantiles: ",quantiles);
display("Round-trip probabilities: ",distcdf("student_t",quantiles,options(df=5)));
figure(1,"Name","Native distributions","NumberTitle","off");
histplot(draws.values,-5:0.25:5);
title("Seeded Student-t samples");xlabel("Value");ylabel("Count");
record counts=histogram(draws.values,-5:0.25:5);
display("Included / excluded samples: ",counts.included,counts.excluded);
% Other families include normal,uniform,exponential,gamma,lognormal,weibull,
% beta,chi_squared,fisher_f,poisson,binomial,bernoulli,negative_binomial,
% laplace,cauchy and logistic. Parameters are explicit typed options.
