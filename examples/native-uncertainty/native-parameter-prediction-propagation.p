native function real decay(real amplitude,real rate,real t){return amplitude*exp(-rate*t);}
% These are explicitly supplied parameter samples; Studio does not invent a
% prior/posterior or infer a probability model from arbitrary multistart fits.
matrix samples=[2,0.7;2.1,0.72;1.9,0.68;2.05,0.69;1.95,0.71];
vector t=linspace(0,4,101);
record parameter_samples=propagateprediction(decay,samples,t,options(threads=4));
record bands=predictionbands(parameter_samples);
figure(1,"Name","Native parameter uncertainty propagation");
plot(t,bands.fitted_mean);hold("on");plot(t,bands.fitted_lower);plot(t,bands.fitted_upper);
xlabel("t");ylabel("Predicted value");
display("Native structural key: ",parameter_samples.cache_key);
display(bands.notice);
