% True continuation passes a fold by tracking arclength, not parameter alone.
% Independent reference: equilibria satisfy mu=-x^2; stability eigenvalue=2*x.
clc;
clear;
close("all");
ode saddle_node(t, real x; real mu) {
    x' = mu+x*x;
}
record branch = continueequilibrium(saddle_node, -1.0,
    options(parameters=options(mu=-1.0), parameter="mu",
        points=36, step=0.1, max_step=0.1,
        tolerance=1e-9));
display("Equilibrium branch: ", branch.status);
display("Largest equation residual: ", max(branch.residuals));
display("Sampled fold candidate flags: ", branch.fold_candidate);
display("Bifurcations refined/certified: ", branch.bifurcation_refined);
figure(1, "Name", "Pseudo-arclength through a saddle-node fold", "NumberTitle", "off");
subplot(2,2,1);
plot(branch.parameter, branch.x(:,1));
title("Equilibrium branch $\\mu=-x^2$"); xlabel("$\\mu$"); ylabel("$x$");
subplot(2,2,2);
plot(branch.parameter, branch.values_real(:,1));
title("Stability eigenvalue $2x$"); xlabel("$\\mu$"); ylabel("$\\lambda$");
subplot(2,2,3);
plot(branch.x(:,1), branch.stable);
title("Stable branch indicator"); xlabel("$x$"); ylabel("Numerically stable");
subplot(2,2,4);
plot(branch.x(:,1), branch.fold_candidate);
title("Fold candidate, requiring refinement"); xlabel("$x$"); ylabel("Candidate flag");
