% Original algebra-research example: adapted frame and multiplication energy.
use_algebra("A_gamma",gamma=1);
clc;clear;close("all");
real algebra_gamma=1.0;
pentarnion a=(i+j+k)/sqrt(3);
pentarnion b=(i-j)/sqrt(2);
pentarnion c=(i+j-2*k)/sqrt(6);
pentarnion h=(sqrt(3)*a+algebra_gamma*ell)/sqrt(3+algebra_gamma^2);
pentarnion normal=(-algebra_gamma*a+sqrt(3)*ell)/sqrt(3+algebra_gamma^2);
assert(abs(norm2(h)-1)<1e-14&&abs(norm2(normal)-1)<1e-14);
assert(abs(algebra_gamma*(coeff(h,1)+coeff(h,2)+coeff(h,3))-3*coeff(h,4))<1e-14);

% Compute the paper's energy using all SIX ordered algebra products.
native function real multiplication_energy(pentarnion x){
    return (norm2(i*x)+norm2(x*i)+norm2(j*x)+norm2(x*j)+norm2(k*x)+norm2(x*k))/4;
}
real A=3*(algebra_gamma^4+2*algebra_gamma^2+3)/((1+algebra_gamma^2)*(3+algebra_gamma^2));
real B=(4*algebra_gamma^2+3)/(1+algebra_gamma^2);
real yh=0.3;real yb=-0.4;real yc=sqrt(0.75);
pentarnion point=yh*h+yb*b+yc*c;
real table_energy=multiplication_energy(point);
real formula_energy=(A*yh^2+B*(yb^2+yc^2))/2;
assert(abs(table_energy-formula_energy)<1e-13);
display("Multiplication energy / independent paper formula:",table_energy,formula_energy);

% Real alternating form in the ORTHONORMAL coordinate order (a,b,c,ell).
% Coefficients of degree3 forms are (abc,abell,acell,bcell).
real kappa=1/sqrt(1+algebra_gamma^2);
real sigma=algebra_gamma/sqrt(1+algebra_gamma^2);
record phi=dform(4,3,[kappa,0,0,sigma/sqrt(3)]);
record dual=hodge(phi);
assert(norm(dual.coefficients-[-sigma/sqrt(3),0,0,kappa])<1e-13);
display("Hodge dual of the alternating three-form:",dual.coefficients);

% The unit sphere in the tilted H_gamma is an ISOMETRIC real sphere.
% Changing algebra energy does not change its induced Gaussian curvature.
native function pentarnion sphere_in_H(real u,real v,real algebra_gamma){
    return sin(u)*cos(v)*(i+j+k+algebra_gamma*ell)/sqrt(3+algebra_gamma^2)
        +sin(u)*sin(v)*(i-j)/sqrt(2)+cos(u)*(i+j-2*k)/sqrt(6);
}
record geometry=chartgeometry(@sphere_in_H,[0.8,0.4],options(parameters=options(algebra_gamma=algebra_gamma),metric="euclidean"));
assert(geometry.embedding_dimension==5&&abs(geometry.gaussian_curvature-1)<1e-12);
display("All five embedded coefficients:",geometry.point);
display("Induced sphere Gaussian curvature:",geometry.gaussian_curvature);
figure(1,"Name","Paper distinguished space H_gamma","NumberTitle","off");
plot3("i","j","ell",h,b,c,normal);
title("Distinguished basis and its normal: projected coefficients");
caption("Euclidean unit sphere curvature is independent of the energy anisotropy.");
