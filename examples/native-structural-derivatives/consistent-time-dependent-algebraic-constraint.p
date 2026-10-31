% Native residual, analytic time/state derivatives, and no user Jacobian.
native function [real differential,real constraint] residual(
    real t,real x,real z,real xp,real zp,real rate){
    return xp+rate*x,z^3-x*x-sin(t);
}
record diagnostic=daestructure(residual,0.3,[1.2,1],[5,6],
    options(parameters=options(rate=2.0)));
display("Conditional structural index and local matrix check: ",diagnostic);
record initial=daeinitialize(residual,0.3,[1.2,1],[5,6],
    options(parameters=options(rate=2.0),residual_tolerance=1e-12));
display("Consistent y, complete y prime, and differentiated constraint: ",initial);
record solved=daesolve(residual,initial.consistent_initial,initial.consistent_derivative,
    options(parameters=options(rate=2.0),algebraic=initial.algebraic,
    t0=0.3,t1=1.5,sample_step=0.01,initialization="check"));
figure(1,"Name","Native index-one constrained evolution");
subplot(2,1,1);plot(solved.t,solved.values(:,1));xlabel("t");ylabel("x");
subplot(2,1,2);plot(solved.t,solved.values(:,2));xlabel("t");ylabel("z");
caption("z cubed equals x squared plus sin(t); full initial derivatives satisfy its time derivative.");
