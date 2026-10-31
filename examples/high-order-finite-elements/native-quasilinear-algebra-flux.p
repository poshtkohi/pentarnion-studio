% Ordered, state-dependent quaternion diffusion with compiler-generated AD.
algebra H=builtin_algebra("quaternion");
native function [real x,real y] plane(real u,real v){return u,v;}
native function [element<H> fx,element<H> fy,element<H> reaction] flux(
    element<H> q,element<H> qx,element<H> qy,real t,real x,real y){
    element<H> D=1+.01*q;
    return D*qx,D*qy,q;
}
native function element<H> exact(real t,real x,real y){return (x+y)*(1+.1*H.i);}
native function element<H> forcing(real t,real x,real y){
    element<H> direction=1+.1*H.i;
    return (x+y)*direction-.02*(direction*direction);
}
record mesh=paramesh(plane,linspace(0,1,4),linspace(0,1,4));
% Explicit per-real-coefficient orders; complete quaternion packing is retained.
record field=femmixed(flux,mesh,options(orders=[1,2,3,1],forcing=forcing,
    boundary_value=exact,exact=exact,threads=0));
assert(field.converged&&field.l2_error<1e-8);
assert(field.components==4&&field.jacobian_evaluations>0);
display("Full typed state layout: ",field.state_layout);
display("Native weak flux/AD residual: ",field.residual_norm);
figure(1,"Name","Ordered algebra-valued quasilinear FEM","NumberTitle","off");
subplot(1,2,1);femmixedplot(field,options(component=0));title("Scalar coefficient");
subplot(1,2,2);femmixedplot(field,options(component=1));title("Quaternion i coefficient");
