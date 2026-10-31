ode hopf(t,real x,real y;real mu){
    x'=mu*x-y-x*(x*x+y*y);
    y'=x+mu*y-y*(x*x+y*y);
}
record branch=continueequilibrium(hopf,[0,0],options(
    parameters=options(mu=-0.3),parameter="mu",points=15,step=0.05,
    min_parameter=-1,max_parameter=1,auto_refine=1));
display("Refined Hopf parameter and angular frequency: ",
    branch.markers.parameter,branch.markers.frequency_or_period);
display(branch.markers.statuses);
figure(1,"Name","Hopf branch stability and refined candidate");
branchplot(branch,options(quantity="measure"));
xlabel("mu");ylabel("Largest real part of equilibrium eigenvalues");
caption("The Hopf location/transversality are tested; criticality is not inferred.");
