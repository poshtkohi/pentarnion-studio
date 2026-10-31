native function matrix<2,2> polar_metric(real r,real theta) {return [1,0;0,r*r];}
native function matrix<2,2> inverse_metric(real r,real theta) {return [1,0;0,1/(r*r)];}
native function matrix<2,2> polar_frame(real r,real theta) {return [1,0;0,1/r];}
native function [real x,real y] cartesian(real r,real theta) {return r*cos(theta),r*sin(theta);}
record geometry=intrinsicgeometry(polar_metric,[2,.4],options(frame=polar_frame));
record derivative=tensorcovariant(inverse_metric,polar_metric,[2,.4],options(variance=[1,1]));
record transformed=charttransition(cartesian,[2,.4],options(
    metric=geometry.metric,connection=geometry.connection,vector=[1,0],covector=[1,0]));
display("Polar frame metric / noncoordinate bracket: ",geometry.frame_metric,geometry.frame_bracket);
display("Covariant derivative of inverse metric, expected zero: ",derivative.tensor_covariant);
display("Cartesian metric / connection, expected identity / zero: ",transformed.metric,transformed.connection);
