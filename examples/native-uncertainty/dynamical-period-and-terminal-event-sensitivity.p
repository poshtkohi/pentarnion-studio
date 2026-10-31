ode oscillator(t,real x,real y;real omega){x'=-omega*y;y'=omega*x;}
record period_effect=modelsensitivity(oscillator,[1],[2],options(
    initial=[1,0],parameters=options(omega=1.5),summary="period",
    method="morris",samples=4,levels=4,threads=4,
    solver=options(t1=24,sample_step=0.02)));
display("Morris influence of angular frequency on sampled period: ",period_effect.mu_star);
ode decay(t,real x;real rate){x'=-rate*x;}
native function real half_level(real t,real x,real rate){return x-0.5;}
record event_effect=modelsensitivity(decay,[0.5],[1.5],options(
    initial=1.0,parameters=options(rate=1.0),summary="event_time",
    method="morris",samples=4,levels=4,threads=4,
    solver=options(t1=5,sample_step=0.05,mass=[2],event=half_level,
        event_direction=-1,event_terminal=1)));
display("Rate influence on localized half-level time with mass2: ",event_effect.mu_star);
% Exact references: oscillator period=2*pi/omega; event time=2*log(2)/rate.
