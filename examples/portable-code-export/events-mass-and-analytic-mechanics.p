% Export with --scientific-adapters plus the chosen target BDF adapter flag.
ode nonlinear_decay(t,real x;real rate) { x'=-rate*x*x; }
native function real threshold(real t,real x,real rate) { return x-.5; }
solution stopped=odesolve(nonlinear_decay,x=1.,rate=2.,t0=0.,t1=1.,sample_step=.1,rtol=1e-10,atol=1e-12,sensitivities="all",events=threshold,event_direction=-1,event_terminal=1);
display("Localized stop time and state, expected .5:",stopped.t(end),stopped.x(end));
display("Fixed-time parameter/initial sensitivities:",odesens(stopped));
native function real mass_rhs(real t,real x,real rate) { return -(1+t+x)*rate*x; }
native function matrix<1,1> state_mass(real t,real x) { return [1+t+x]; }
record regular=masssolve(mass_rhs,state_mass,1.,-2.,options(parameters=options(rate=2.),t1=1.,rtol=1e-10,atol=1e-12));
display("State/time mass final value, expected exp(-2):",regular.values(end));
native function real mechanical_lagrangian(real t,real q,real v) { return .5*v*v-.5*q*q; }
native function real mechanical_hamiltonian(real t,real q,real p) { return .5*p*p+.5*q*q; }
record lagrange=mechanics(mechanical_lagrangian,1.,0.,options(form="lagrangian",t1=1.,time_step=.005,sample_step=.02));
record hamilton=mechanics(mechanical_hamiltonian,1.,0.,options(form="hamiltonian",method="midpoint",t1=1.,time_step=.005,sample_step=.02));
display("Generated Euler-Lagrange coordinate:",lagrange.q(end));
display("Generated Hamiltonian momentum and energy balance:",hamilton.p(end),hamilton.energy_balance(end));
figure(1);plot(hamilton.q,hamilton.p);title("Canonical harmonic motion");
