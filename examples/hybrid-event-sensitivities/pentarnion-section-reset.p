use_algebra("A_gamma",gamma=1);
clc;clear;
ode pentflow(t,pentarnion p;pentarnion drive) {
    p'=drive;
}
native function real section(real t,pentarnion p,pentarnion drive) {
    return coeff(p,0)-1.0;
}
native function pentarnion flip(real t,pentarnion p,pentarnion drive) {
    return conj(p);
}
native function real terminal_magnitude(real t,pentarnion p,pentarnion drive) {
    return norm2(p);
}
pentarnion initial=.2*i+.3*j+.4*k+.5*ell;
pentarnion drive=1+.1*i+.2*j+.3*k+.4*ell;
record trajectory=hybridsolve(pentflow,section,flip,initial,
    options(parameters=options(drive=drive),t1=2.0,event_direction=1,
        sensitivities="parameters"));
display("All final coefficients [1,i,j,k,ell], expected [2,-.2,-.3,-.4,-.5]: ",
    trajectory.values(end,:));
display("Five-parameter event-time derivatives: ",trajectory.event_time_derivatives);
record reverse=hybridadjoint(pentflow,section,flip,terminal_magnitude,initial,
    options(parameters=options(drive=drive),t1=2.0,event_direction=1,max_steps=100000));
display("Full native Pentarnion parameter gradient; expected [6.4,0,0,0,0]: ",
    reverse.adjoint.parameter_gradient);
