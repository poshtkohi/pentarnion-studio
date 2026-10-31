native function real source(real u,real t,real x) {
    return 1.0;
}
native function real prescribed(real t,real tag,real x,real normal) {
    return t+x;
}
native function real robin_data(real t,real tag,real x,real normal) {
    return .2*normal+2*(t+x);
}
matrix x=linspace(0,1,33);
record field=pdeimplicit(source,x,x,options(
    diffusion=.2,t1=.2,time_step=.01,sample_step=.02,adaptive=0,
    boundaries=options(
        left=options(type="dirichlet",value=prescribed),
        right=options(type="robin",value=robin_data,alpha=2.0))));
matrix U=pdeimplicitcoeff(field,0);
display("Manufactured final-profile error: ",max(abs(U(end,:)-x-.2)));
figure(1,"Name","Dynamic mixed boundaries","NumberTitle","off");
plot(x,U(end,:));title("$u(x,t)=x+t$: Dirichlet left, Robin right");
xlabel("x");ylabel("u");
