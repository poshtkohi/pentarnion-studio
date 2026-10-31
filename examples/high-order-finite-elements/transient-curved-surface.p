% Curved-surface high-order FEM: consistent mass and implicit decay.
native function [real x,real y] plane(real u,real v){return u,v;}
native function [real x,real y,real z] surface(real u,real v){return u,v,u*u+v*v;}
native function real decay(real u,real t,real x,real y,real z){return u;}
native function real initial(real t,real x,real y,real z){return 1.0;}
% Constant-in-space decay makes an independent curved-metric check possible.
record mesh=paramesh(surface,linspace(0,1,4),linspace(0,1,4));
record parameter_mesh=paramesh(plane,linspace(0,1,4),linspace(0,1,4));
record simulation=femtime(decay,mesh,options(geometry=surface,
    parameter_vertices=parameter_mesh.vertices,initial=initial,order=3,
    boundary="neumann",adaptive=0,t1=.1,time_step=.01,sample_step=.02));
matrix values=femtimecoeff(simulation,0);
assert(abs(values(end,1)-(1.01)^(-10))<1e-8);
figure(1,"Name","Curved-surface implicit H1 evolution","NumberTitle","off");
subplot(1,2,1);plot(simulation.t,values(:,1));title("Consistent mass on the curved surface");
subplot(1,2,2);femsystemplot(femtimeslice(simulation,length(simulation.t)));
title("Final sampled surface, full H1 data retained");
display("Native surface/time integration: ",simulation.status);
