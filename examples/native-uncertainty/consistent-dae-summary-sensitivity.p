% Index-one model: x'=-rate*x, z=x^2. Compiler-generated residual Jacobians.
native function [real r1,real r2] residual(real t,real x,real z,real xp,real zp,real rate){
    return xp+rate*x,z-x*x;
}
record influence=modelsensitivity(residual,[0.5],[1.5],options(
    kind="dae",initial=[1,1],initial_derivative=[-1,-2],algebraic=[0,1],
    parameters=options(rate=1.0),summary="final",coefficient=2,
    method="morris",samples=4,levels=4,threads=4,
    solver=options(t1=1,sample_step=0.05,initialization="consistent")));
display("Rate influence on the final algebraic state z: ",influence.mu_star);
display(influence.solver);
% For each varied rate, native IDAS recomputes a consistent index-one initial
% condition. No explicit-ODE Jacobian is substituted for the DAE residual.
