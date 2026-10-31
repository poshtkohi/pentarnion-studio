native function [real x,real y] plane(real u,real v) {return u,v;}
native function real reaction(real u,real x,real y) {return 0.0;}
native function real forcing(real x,real y) {
    real a=80.0;
    real A=x*(1-x);real B=y*(1-y);
    real dx=x-.3;real dy=y-.3;
    real e=exp(-a*(dx*dx+dy*dy));
    real uxx=B*e*(-2-4*a*dx*(1-2*x)+A*(4*a*a*dx*dx-2*a));
    real uyy=A*e*(-2-4*a*dy*(1-2*y)+B*(4*a*a*dy*dy-2*a));
    return -uxx-uyy;
}
native function real exact(real x,real y) {
    return x*(1-x)*y*(1-y)*exp(-80*((x-.3)^2+(y-.3)^2));
}
record mesh=paramesh(plane,linspace(0,1,5),linspace(0,1,5));
record report=femadapt(reaction,mesh,options(order=1,forcing=forcing,exact=exact,
    spatial_levels=3,spatial_max_nodes=3000,spatial_rtol=.05,
    spatial_atol=1e-5,mark_fraction=.6,threads=4));
display("Adaptive status / finite-grid difference: ",report.status,report.scaled_difference);
display("History[level,coarse nodes,trial nodes,faces,scaled,absolute,marked,work,seconds]: ",report.history);
display("Independent exact-solution L2 error: ",report.field.l2_error);
figure(1,"Name","Conforming local H1 adaptation","NumberTitle","off");
femsystemplot(report.field);title("Localized manufactured field with red/green refinement");
