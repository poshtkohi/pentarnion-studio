% Equilibria satisfy mu=x^2; the analytic fold is (mu,x)=(0,0).
ode fold(t,real x;real mu){x'=mu-x*x;}
record branch=continueequilibrium(fold,1,options(parameters=options(mu=1.0),
    parameter="mu",points=40,step=-0.1,min_step=0.001,max_step=0.1,
    min_parameter=-0.1,max_parameter=2,auto_refine=1,
    refinement=options(max_candidates=8,tolerance=1e-8)));
display("Accepted refined markers: ",branch.markers.accepted_count);
display("Parameter values: ",branch.markers.parameter);
display("Source candidate rows: ",branch.markers.candidate_rows);
display(branch.markers.statuses);
figure(1,"Name","Fold branch and native refined marker");
branchplot(branch,options(coordinate=1));xlabel("mu");ylabel("x");
caption("Green: stable; red: unstable; grey: inconclusive; crosses: accepted refined candidates.");
