% Compiled charts; metrics are explicitly induced by the Euclidean embedding.
use_algebra("A_gamma",gamma=1);clc;clear;
native function [real x,real y,real z] plane(real u,real v){return u,v,0.0;}
native function [real x,real y,real z] cylinder(real u,real v,real radius){return radius*cos(u),radius*sin(u),v;}
native function [real x,real y,real z] sphere(real u,real v,real radius){return radius*sin(u)*cos(v),radius*sin(u)*sin(v),radius*cos(u);}
record flat=chartgeometry(plane,[.8,.4],options(metric="euclidean"));
record tube=chartgeometry(cylinder,[.8,.4],options(metric="euclidean",parameters=options(radius=2.0)));
record ball=chartgeometry(sphere,[.8,.4],options(metric="euclidean",parameters=options(radius=2.0)));
display("Plane / cylinder / sphere Gaussian curvature:",flat.gaussian_curvature,tube.gaussian_curvature,ball.gaussian_curvature);
assert(abs(flat.gaussian_curvature)<1e-12&&abs(tube.gaussian_curvature)<1e-12&&abs(ball.gaussian_curvature-.25)<1e-12);
display("Sphere metric:",ball.metric);
display("Sphere Christoffel rows a, columns (11,12,21,22):",ball.christoffel);
display("Sphere mean-curvature vector:",ball.mean_curvature_vector);
% A custom metric is independent of the display embedding.
native function [real E,real F,real G] sphere_metric(real u,real v,real radius){return radius*radius,0.0,radius*radius*sin(u)*sin(u);}
record intrinsic=chartgeometry(plane,[.8,.4],options(metric=sphere_metric,metric_parameters=options(radius=2.0)));
assert(abs(intrinsic.gaussian_curvature-.25)<1e-12&&intrinsic.extrinsic_available==0);
display("Intrinsic sphere metric on a plane coordinate chart:",intrinsic.gaussian_curvature);
