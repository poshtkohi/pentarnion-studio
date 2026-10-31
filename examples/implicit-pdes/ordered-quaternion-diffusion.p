% The compiler preserves all four coefficients and explicit multiplication side.
algebra H=builtin_algebra("quaternion");
native function element<H> reaction(element<H> u,real t,real x) {
    return 0*u;
}
native function element<H> diffusivity(real t,real x,real scale) {
    return scale*(1+.5*H.i);
}
real pi=3.14159265358979323846;
matrix x=linspace(0,1,33);
matrix initial=zeros(33,4);
for(row=1:33) {
    initial(row,3)=cos(pi*x(row));
}
record left=pdeimplicit(reaction,x,initial,options(
    diffusion=diffusivity,diffusion_parameters=options(scale=.1),diffusion_side="left",
    t1=.1,time_step=.01,sample_step=.01,adaptive=0,threads=2));
record right=pdeimplicit(reaction,x,initial,options(
    diffusion=diffusivity,diffusion_parameters=options(scale=.1),diffusion_side="right",
    t1=.1,time_step=.01,sample_step=.01,adaptive=0,threads=2));
matrix L=pdeimplicitcoeff(left,3);
matrix R=pdeimplicitcoeff(right,3);
display("Same compiled callbacks: ",left.cache_keys==right.cache_keys);
figure(1,"Name","Ordered quaternion diffusion","NumberTitle","off");
subplot(1,2,1);plot(x,L(end,:));title("Left multiplication: final $k$ coefficient");
subplot(1,2,2);plot(x,R(end,:));title("Right multiplication: final $k$ coefficient");
