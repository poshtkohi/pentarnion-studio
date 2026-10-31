% Analytic4D periodic normal form; critical multiplier=-exp(6.283185307179586*mu).
ode flip(t,real x,real y,real u,real v;real mu){
    x'=-y+(1-x*x-y*y)*x;
    y'=x+(1-x*x-y*y)*y;
    u'=((mu-1)/2+(mu+1)*x/2)*u+((mu+1)*y/2-0.5)*v;
    v'=((mu+1)*y/2+0.5)*u+((mu-1)/2-(mu+1)*x/2)*v;
}
record branch=continueperiodic(flip,[1,0,0,0],options(
    parameters=options(mu=-0.03),parameter="mu",period=6.283185307179586,
    points=3,step=0.04,min_step=0.002,max_step=0.04,
    min_parameter=-0.1,max_parameter=0.1,rtol=1e-7,atol=1e-9,
    auto_refine=1,refinement=options(fd_agreement=0.01)));
display("Refined period-doubling parameter (analytic zero): ",branch.markers.parameter);
display("Refined physical period (analytic6.283185307179586): ",branch.markers.frequency_or_period);
display(branch.markers.statuses);
figure(1,"Name","Periodic branch: Floquet stability and refined flip");
branchplot(branch,options(quantity="measure"));
xlabel("mu");ylabel("Largest transverse Floquet modulus");
caption("The crossing is a numerical candidate; cubic criticality is not computed.");
