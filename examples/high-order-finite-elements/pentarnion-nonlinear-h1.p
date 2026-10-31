use_algebra("A_gamma",gamma=1);
native function [real x,real y] plane(real u,real v) {return u,v;}
native function pentarnion reaction(pentarnion p,real x,real y) {return (p*p)*p;}
native function pentarnion exact(real x,real y) {return 1+.1*i+.2*j+.05*ell;}
native function pentarnion forcing(real x,real y) {
    pentarnion p=1+.1*i+.2*j+.05*ell;
    return (p*p)*p;
}
record mesh=paramesh(plane,linspace(0,1,3),linspace(0,1,3));
record field=femsystem(reaction,mesh,options(order=3,forcing=forcing,
    boundary_value=exact,exact=exact,diffusion=1+.05*i,
    diffusion_side="left",threads=2));
display("Complete five-coordinate H1 field / error: ",field.components,field.l2_error);
display("Full retained nodal values: ",field.dof_values);
figure(1,"Name","Nonlinear Pentarnion FEM","NumberTitle","off");
femsystemplot(field,options(component=1));title("Retained $i$ coefficient of a native Pentarnion solution");
