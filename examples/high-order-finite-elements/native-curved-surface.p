native function [real x,real y] parameter_plane(real u,real v) {return u,v;}
native function [real x,real y,real z] paraboloid(real u,real v) {return u,v,u*u+v*v;}
native function real reaction(real w,real x,real y,real z) {return w*w*w;}
native function real forcing(real x,real y,real z) {
    real r2=x*x+y*y;
    return 8*x*(1+2*r2)/((1+4*r2)*(1+4*r2))+x*x*x;
}
native function real exact(real x,real y,real z) {return x;}
matrix grid_axis=linspace(0,1,5);
record mesh=paramesh(paraboloid,grid_axis,grid_axis);
record parameters=paramesh(parameter_plane,grid_axis,grid_axis);
record field=femsystem(reaction,mesh,options(order=3,geometry=paraboloid,
    parameter_vertices=parameters.vertices,forcing=forcing,
    boundary_value=exact,exact=exact,threads=4));
display("Curved chart geometry / surface exact-solution L2 error: ",field.geometry_kind,field.l2_error);
figure(1,"Name","Native curved-surface nonlinear FEM","NumberTitle","off");
femsystemplot(field);title("Curved paraboloid: $-\\Delta_g w+w^3=f$");
