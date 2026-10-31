use_algebra("A_gamma",gamma=1);
clc;clear;
% Equilibrium is(x,y)=(mu,0); along it the critical real part is1.5*mu.
% A fixed-state parameter derivative alone would miss part of this crossing.
ode shifted_cycle(t,real x,real y;real mu) {
    x'=(mu+.5*x)*(x-mu)-y-((x-mu)*(x-mu)+y*y)*(x-mu);
    y'=x-mu+(mu+.5*x)*y-((x-mu)*(x-mu)+y*y)*y;
}
record point=refinebifurcation(shifted_cycle,[.1,0],
    options(kind="hopf",parameter="mu",parameters=options(mu=.1),tolerance=1e-9));
display("Refined state/parameter, expected [0,0],0: ",point.x,point.parameter);
display("Angular frequency, expected1: ",point.frequency);
display("Eigenvalue crossing along the equilibrium branch, expected1.5: ",
    point.crossing_derivative);
display("Necessary-condition flags/status: ",point.simple,point.transverse,point.status);
display("Criticality computed (0: no AD3/first-Lyapunov claim): ",point.criticality_computed);
