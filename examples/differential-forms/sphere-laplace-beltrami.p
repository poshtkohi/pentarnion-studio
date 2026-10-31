% The sphere's l=1 eigenfunction satisfies Delta_S cos(u) = -2 cos(u).
clc; clear;
native function [real px,real py,real pz] sphere_chart(real u,real v) {
    return sin(u)*cos(v),sin(u)*sin(v),cos(u);
}
native function real height_on_sphere(real u,real v) { return cos(u); }
real u0=1.1;
real v0=0.4;
real computed = laplacebeltrami(@sphere_chart,@height_on_sphere,[u0,v0]);
real analytic = -2*cos(u0);
display("Native analytic chart Laplace-Beltrami: ",computed);
display("Independent sphere eigenvalue identity: ",analytic);
assert(abs(computed-analytic)<1e-12);
display("Chart curvature and metric: ",chartgeometry(@sphere_chart,[u0,v0]));
