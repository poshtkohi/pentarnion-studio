use_algebra("A_gamma", gamma=1);
clc; clear; close("all");

% The exact exponents are -1 and -2. The compiler supplies all tangent equations.
ode diagonal(t,real x,real y) { x'=-x; y'=-2*y; }
record rates=lyapunov(diagonal,[0,0],options(t1=4,qr_step=0.25,
    rtol=1e-10,atol=1e-12));
assert(norm(rates.exponents-[-1;-2])<1e-6);
display("Exact diagonal exponents [-1;-2]:",rates.exponents);
display("Estimate status and maximum QR orthogonality error:",rates.status,
    rates.orthogonality_error);
record equilibrium_status=stability(diagonal,[0,0]);
assert(equilibrium_status.stable);
display("Equilibrium classification:",equilibrium_status.status);

% One whole Pentarnion state has five tangent coordinates and five exponents.
ode contraction(t,pentarnion p;real rate) { p'=-rate*p; }
record pent_rates=lyapunov(contraction,1+i+j+k+ell,
    options(parameters=options(rate=0.5),t1=2,qr_step=0.25));
assert(norm(pent_rates.exponents+0.5*ones(5,1))<1e-5);
display("Five Pentarnion contraction exponents, all -0.5:",pent_rates.exponents);

native function real halve(real x) { return 0.5*x; }
record map_growth=lyapunov(halve,1.0,
    options(iterations=50,transient=7,qr_stride=4));
assert(abs(map_growth.largest-log(0.5))<1e-12);
display("Discrete-map growth per iteration, expected log(0.5):",map_growth.exponents);

% The Poincare samples come from solver-localized native events, not straight
% line interpolation through coarse saved output samples.
ode oscillator(t,real x,real y) { x'=y; y'=-x; }
native function real section(real time,real x,real y) { return x; }
solution s=odesolve(oscillator,x=1.0,y=0.0,t0=0,t1=12,
    sample_step=0.25,method="adams",events=section,event_direction=1,
    rtol=1e-10,atol=1e-12);
record crossings=poincare(s);
assert(crossings.crossings==2);
assert(abs(crossings.intervals(1)-2*acos(-1))<1e-7);
display("Poincare return time, expected 2*pi:",crossings.intervals);
record recurrence_result=recurrence([0;1;0;1;0],
    options(radius=0.01,lag=2,theiler=1));
assert(norm(recurrence_result.distance)==0);

figure(1,"Name","Validated tangent and section analyses","NumberTitle","off");
subplot(2,2,1);plot(rates.t,rates.history(:,1));
title("Running largest Lyapunov estimate");xlabel("$t$");ylabel("$\\lambda_1$");
vector directions=1:5;
subplot(2,2,2);plot(directions,pent_rates.exponents);
title("Five exponents for one Pentarnion state");xlabel("Coordinate direction");
subplot(2,2,3);plot(s.x,s.y);title("Oscillator trajectory");xlabel("$x$");ylabel("$y$");
subplot(2,2,4);plot(crossings.t,crossings.states(:,2));
title("Positive-direction Poincare crossings");xlabel("$t$");ylabel("$y$");
