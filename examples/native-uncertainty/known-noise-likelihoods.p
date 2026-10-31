native function real constant(real level,real x){return level;}
record multiplicative=multistartfit(constant,[1;3],[1,2,3],[1,2,4],options(
    objective="lognormal_known_variance",noise_variance=0.04,lower=0.01));
display("Lognormal model median (exact geometric mean2): ",multiplicative.best_parameters);
record counts=multistartfit(constant,[1;4],[1,2,3],[1,2,6],options(
    objective="poisson_deviance",lower=0.01));
display("Poisson mean (exact arithmetic mean3): ",counts.best_parameters);
record heavy_tail=multistartfit(constant,[0;1],[1,2,3,4],[0,0,0,100],options(
    objective="student_t_known_scale",noise_variance=1,noise_degrees=4));
display("Known-scale Student-t fit with one large observation: ",heavy_tail.best_parameters);
% noise_variance means log-space variance for lognormal, scale squared for t.
% A lognormal callback predicts its median, not its arithmetic mean.
