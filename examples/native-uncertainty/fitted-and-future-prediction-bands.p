native function real line(real slope,real intercept,real x){return slope*x+intercept;}
vector x=linspace(0,1,12);
vector observed=[1.02,1.05,1.21,1.24,1.42,1.40,1.59,1.61,1.76,1.84,1.87,2.05];
record bootstrap_fit=fitbootstrap(line,[1,1],x,observed,options(
    objective="gaussian_known_variance",noise_variance=0.0025,
    sample_method="parametric",samples=128,seed=42,threads=4));
record bands=predictionbands(bootstrap_fit,options(confidence=0.95,
    noise="gaussian",noise_variance=0.0025,draws=4096,seed=17));
figure(1,"Name","Fitted means and future observations");
plot(x,bands.fitted_mean);hold("on");
plot(x,bands.fitted_lower);plot(x,bands.fitted_upper);
plot(x,bands.future_lower);plot(x,bands.future_upper);
legend("Fitted mean","Fitted lower","Fitted upper","Future lower","Future upper");
xlabel("x");ylabel("Observation");
display("Gridwise simultaneous future lower band: ",bands.future_simultaneous_lower);
display("Gridwise simultaneous future upper band: ",bands.future_simultaneous_upper);
% Future bands explicitly add independent known observation noise. Simultaneous
% calibration applies to this finite prediction grid and empirical ensemble.
