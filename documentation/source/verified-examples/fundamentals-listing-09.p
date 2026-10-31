native function real equation(real x,real a) { return x*x-a; }
record answer = root(equation,[0,2],
    options(parameters=options(a=2.0),xtol=1e-12,ftol=1e-10));
assert(answer.converged);
display(answer.x,answer.residual_norm,answer.status);
