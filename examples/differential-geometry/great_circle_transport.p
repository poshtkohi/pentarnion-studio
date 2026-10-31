% Native RK4 of the chart Levi-Civita equations with compiled AD2 derivatives.
use_algebra("A_gamma",gamma=1);clc;clear;close("all");
native function [real x,real y,real z] sphere(real u,real v,real radius){return radius*sin(u)*cos(v),radius*sin(u)*sin(v),radius*cos(u);}
record path=geodesic(sphere,[.9,.2],[.3,.4],options(parameters=options(radius=2.0),metric="euclidean",t1=2.0,time_step=.005,sample_step=.02,transport=[0,1],lower=[.01,-10],upper=[3.13,10]));
display("Geodesic steps / compiled geometry evaluations:",path.steps,path.evaluations);
display("Maximum sampled relative speed / transported-norm drift:",path.max_relative_speed_drift,path.max_relative_transport_drift);
figure(44,"Name","Sphere geodesic and parallel transport","NumberTitle","off");
subplot(1,2,1);plot3(path.positions(:,1),path.positions(:,2),path.positions(:,3));title("Great-circle segment");xlabel("x");ylabel("y");zlabel("z");
subplot(1,2,2);plot(path.t,path.speed_squared);title("Metric speed squared");xlabel("Affine parameter");ylabel("$g(v,v)$");
% transport stores contravariant chart components; its metric norm is separately retained.
