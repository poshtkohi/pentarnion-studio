% Distinct H1 approximation spaces for two coupled physical scalar fields.
native function [real x,real y] plane(real u,real v){return u,v;}
native function [real fxu,real fxv,real fyu,real fyv,real ru,real rv] coupled(
    real u,real v,real ux,real vx,real uy,real vy,real t,real x,real y){
    return ux,vx,uy,vy,u+.1*v,v+.2*u;
}
native function [real u,real v] exact(real t,real x,real y){return x+y,x*x*x+y*y*y;}
native function [real fu,real fv] forcing(real t,real x,real y){
    real u=x+y;real v=x*x*x+y*y*y;return u+.1*v,-6*u+v+.2*u;
}
record mesh=paramesh(plane,linspace(0,1,4),linspace(0,1,4));
record field=femmixed(coupled,mesh,options(orders=[1,3],forcing=forcing,
    boundary_value=exact,exact=exact,threads=0));
assert(field.converged&&field.l2_error<1e-8);
assert(field.component_offsets(3)-field.component_offsets(2)>
    field.component_offsets(2)-field.component_offsets(1));
display("Independent per-component H1 offsets: ",field.component_offsets);
display("Verified weak residual/L2 error: ",field.residual_norm,field.l2_error);
figure(1,"Name","Mixed H1 spaces: P1 and P3","NumberTitle","off");
subplot(1,2,1);femmixedplot(field,options(component=0));title("P1 field u=x+y");
subplot(1,2,2);femmixedplot(field,options(component=1));title("P3 field v=x^3+y^3");
