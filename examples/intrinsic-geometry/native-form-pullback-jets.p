% Pull back -y dx+x dy by a native polar map, retaining analytic Hessians.
native function [real a,real b] rotation_form(real x,real y){return -y,x;}
native function [real x,real y] polar(real r,real angle){return r*cos(angle),r*sin(angle);}
record form=formpullback(rotation_form,polar,[2.0,.3],1);
assert(norm(form.coefficients-[0,4],"fro")<1e-10);
assert(abs(form.first(2,1)-4)<1e-10);
assert(abs(form.second(2,1)-2)<1e-10);
assert(norm(form.metric-[1,0;0,4],"fro")<1e-10);
record exterior=exteriord(form);
assert(abs(exterior.coefficients(1)-4)<1e-10);
record star=hodge(form);
display("Pullback coefficients (dr,dtheta): ",form.coefficients);
display("Induced metric: ",form.metric);
display("Exterior derivative: ",exterior.coefficients);
display("Hodge star, with retained variable metric jets: ",star.coefficients);

% The third derivative of x^3 matters for the Hessian of a pulled-back 1-form.
native function real cubic_map(real x){return x*x*x;}
native function real one_form(real y){return y*y;}
record cubic=formpullback(one_form,cubic_map,[2.0],1);
assert(abs(cubic.coefficients(1)-768)<1e-9);
assert(abs(cubic.first(1)-3072)<1e-9);
assert(abs(cubic.second(1)-10752)<1e-8);
display("Analytic cubic pullback: ",cubic.coefficients,cubic.first,cubic.second);
