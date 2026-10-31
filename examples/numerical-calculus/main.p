% Category 3: sampled calculus, quadrature and stored-solution interpolation.
use_algebra("A_gamma",gamma=1);
clc;
clear;
close("all");

function real cubic(real x) { return x*x*x; }
function pentarnion polynomial(real x) { return 1+x*i+x*x*j; }

real cubic_area = integral(cubic,0,1);
pentarnion pent_area = integral(polynomial,0,1);
assert(abs(cubic_area-0.25)<1e-9);
display("Integral of x^3 on [0,1] =",cubic_area);
display("Pentarnion integral =",pent_area);

vector x = linspace(0,1,101);
vector linear_values = 1+2*x;
real trapezoid_area = trapz(x,linear_values);
matrix cumulative = cumtrapz(x,linear_values);
assert(abs(trapezoid_area-2)<1e-12);
display("Trapezoidal integral of 1+2x =",trapezoid_area);

matrix tent = interp1([0,0.5,1],[0,1,0],x);
matrix differences = diff([1,4,9,16]);
display("Linear interpolation at x=0.25 =",interp1([0,0.5,1],[0,1,0],0.25));
display("First forward differences =",differences);

[matrix X,matrix Y] = meshgrid([-1,0,2],[0,0.5,2,3]);
matrix F = X.^2+3*Y.^2;
[matrix gx,matrix gy] = gradient(F,[-1,0,2],[0,0.5,2,3]);
matrix lap = laplacian(F,[-1,0,2],[0,0.5,2,3]);
matrix div = divergence(2*X,3*Y,[-1,0,2],[0,0.5,2,3]);
display("Gradient x at last grid point =",gx(end,end));
display("Gradient y at last grid point =",gy(end,end));
display("Laplacian of x^2+3y^2 =",lap(2,2));
display("Divergence of (2x,3y) =",div(2,2));

ode decay(t,pentarnion p;real rate) { p'=-rate*p; }
solution s = odesolve(decay,p=1+i-j+ell,rate=1,t0=0,t1=2,
    sample_step=0.125,method="bdf",jacobian="analytic",rtol=1e-11,atol=1e-13);
vector query = linspace(0,2,201);
matrix dense = deval(s,query,"p");
display("Cubic Hermite evaluation at t=0.0625 =",deval(s,0.0625,"p"));

figure(1,"Name","Category 3: numerical calculus","NumberTitle","off");
subplot(2,2,1);plot(x,linear_values);
title("Sampled linear function");xlabel("$x$");ylabel("$1+2x$");
subplot(2,2,2);plot(x,cumulative);
title("Cumulative trapezoidal integral");xlabel("$x$");ylabel("Integral");
subplot(2,2,3);plot(x,tent);
title("Piecewise linear interpolation");xlabel("$x$");ylabel("Interpolated value");
subplot(2,2,4);plot(query,coeff(dense,1));
title("Stored-solution cubic Hermite evaluation");xlabel("$t$");ylabel("$i$ coefficient");
