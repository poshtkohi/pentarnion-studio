use_algebra("A_gamma",gamma=1);
function [real low,real high] bounds(real center,real radius=1.0) {
    return center-radius,center+radius;
}
[real low,real high]=bounds(radius=2.0,center=10.0);
display("Named/default function and tuple: ",low,high);
record cfg=options(model=options(rate=.7),label="portable values");
display("Nested record rate: ",cfg.model.rate);
real gain=2.0;
callback captured=@(real x)=>gain*x;
gain=99.0;
display("Snapshot lambda, expected 10: ",captured(5.0));
complex z=complex(.2,.3);
display("Complex exponential: ",exp(z));
matrix A=[3,1;1,2];
display("Real equation solution: ",linsolve(A,A*[2;-1]));
pentarnion a=2+.2*i+.3*j;
pentarnion p=1-i+.5*ell;
pentarnion left_solution=a\(a*p);
pentarnion right_solution=(p*a)/a;
display("Ordered left and right equation solutions: ",left_solution,right_solution);
assert(norm2(left_solution-p)<1e-20&&norm2(right_solution-p)<1e-20);
