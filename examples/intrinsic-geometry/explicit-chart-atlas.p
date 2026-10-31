native function matrix<2,2> flat_metric(real x,real y) {return [1,0;0,1];}
native function matrix<2,2> shear_metric(real x,real y) {return [1+4*x*x,-2*x;-2*x,1];}
native function [real x,real y] shear(real u,real v) {return u,v+u*u;}
native function [real u,real v] unshear(real x,real y) {return x,y-x*x;}
record atlas=chartatlas(
    options(cartesian=options(metric=flat_metric,lower=[-3,-3],upper=[3,3]),
            sheared=options(metric=shear_metric,lower=[-3,-3],upper=[3,9])),
    options(forward=options(from="cartesian",to="sheared",map=shear),
            backward=options(from="sheared",to="cartesian",map=unshear)));
record source=atlasgeometry(atlas,"cartesian",[.4,.2]);
record target=atlasgeometry(atlas,"sheared",[.4,.36]);
record change=atlastransition(atlas,"cartesian","sheared",[.4,.2],options(
    metric=source.metric,connection=source.connection,vector=[1,0]));
display("Explicit transition / target coordinates: ",change.path,change.coordinates);
display("Transformed and independently sampled target metrics: ",change.metric,target.metric);
display("Both charts have zero curvature: ",source.scalar_curvature,target.scalar_curvature);
