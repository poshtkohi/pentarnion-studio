clc;clear;close("all");
ode decay(t,real y;real rate){y'=-rate*y;}
vector times=[0,0.25,0.5,1,1.5,2];
% Synthetic deterministic perturbations are descriptive data, not sampled errors.
matrix observed=(2*exp(-0.7*times)+0.01*sin(7*times))';
record starts=odemultistartfit(decay,[0.2;0.5;1.1;1.6],times,observed,
    options(initial=2.0,parameters=options(rate=0.3),threads=4));
record profile=odeprofilefit(decay,0.3,times,observed,1,linspace(0.6,0.8,11),
    options(initial=2.0,parameters=options(rate=0.3),threads=4));
record boot_result=odefitbootstrap(decay,0.3,times,observed,
    options(initial=2.0,parameters=options(rate=0.3),samples=24,
            sample_method="residual",threads=4,seed=42));
display("Independent ODE multistart rates:",starts.parameters);
display("Residual-bootstrap parameter spread:",boot_result.parameter_std);
figure(1,"Name","Native ODE refits","NumberTitle","off");
subplot(1,2,1);plot(profile.grid,profile.delta_sse);title("Local profile SSE");xlabel("Rate");
subplot(1,2,2);plot(times,observed);title("Observed native-solver fit data");xlabel("Time");
% Bootstrap bands are pointwise fitted-mean bands under the chosen residual
% resampling assumptions; they are not future-observation prediction intervals.
