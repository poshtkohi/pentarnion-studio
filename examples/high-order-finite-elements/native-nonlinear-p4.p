native function [real x,real y] plane(real u,real v) {return u,v;}
native function real reaction(real u,real x,real y) {return u*u*u;}
native function real forcing(real x,real y,real scale) {
    real u=scale*x*(1-x)*y*(1-y);
    return 2*scale*(x*(1-x)+y*(1-y))+u*u*u;
}
native function real exact(real x,real y,real scale) {return scale*x*(1-x)*y*(1-y);}
record mesh=paramesh(plane,linspace(0,1,5),linspace(0,1,5));
record field=femsystem(reaction,mesh,options(order=4,forcing=forcing,
    forcing_parameters=options(scale=1.0),exact=exact,
    exact_parameters=options(scale=1.0),threads=4));
display("Native nonlinear P4 solve / independently verified residual: ",field.status,field.residual_norm);
display("Manufactured degree4 exact-solution L2 error: ",field.l2_error);
display("Compiler-generated Jacobian evaluations: ",field.jacobian_evaluations);
display("Shared parallel operations / workers: ",field.parallel_calls,field.threads_used);
figure(1,"Name","Native nonlinear P4 FEM","NumberTitle","off");
femsystemplot(field);title("High-order solution of $-\\Delta u+u^3=f$");
