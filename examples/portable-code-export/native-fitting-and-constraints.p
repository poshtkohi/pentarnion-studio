% Run in Studio, or export with --scientific-adapters after reviewing its report.
native function [real first, real second] residual(real a, real b) {
    return a-2.0, b+1.0;
}
record fit=lsqfit(residual,[0,0],options(covariance=1,noise_variance=.04));
display("Fitted coordinates and conditional covariance:",fit.x,fit.covariance);

native function real objective(real x,real y) {
    return (x-2)*(x-2)+(y-1)*(y-1);
}
native function real halfplane(real x,real y) { return x+y-1; }
record minimum=minimize(objective,[0,0],options(algorithm="slsqp",inequality=halfplane,max_evaluations=1000));
display("Constrained minimum, expected [1,0]:",minimum.x);
display("Feasible, converged, objective:",minimum.feasible,minimum.converged,minimum.objective);
