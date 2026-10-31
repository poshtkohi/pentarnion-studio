use_algebra("A_gamma",gamma=1);
clc;clear;close("all");
ode saddle_node(t,real x;real mu) {
    x'=mu+x*x;
}
record branch=continueequilibrium(saddle_node,-1.0,
    options(parameters=options(mu=-1.0),parameter="mu",points=30,
        step=.1,max_step=.1,tolerance=1e-8));
record point=refinebifurcation(saddle_node,[.2],
    options(kind="fold",parameter="mu",parameters=options(mu=-.05),tolerance=1e-9));
display("Refined fold, expected x=mu=0: ",point.x,point.parameter);
display("Residual/simple/transverse/quadratic-nondegenerate: ",point.residual,
    point.simple,point.transverse,point.nondegenerate);
display("Native AD2 corrector: ",point.corrector_derivatives);
figure(1,"Name","Arclength branch and native fold refinement","NumberTitle","off");
subplot(1,2,1);plot(branch.parameter,branch.x(:,1));
title("Equilibrium branch, refined fold printed in Command Window");xlabel("mu");ylabel("x");
subplot(1,2,2);plot(point.history(:,1),point.history(:,2));
title("Augmented fold residual");xlabel("Newton iteration");ylabel("Residual");
