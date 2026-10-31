use_algebra("A_gamma", gamma=1);
clc;
clear;

native function pentarnion field(pentarnion p, pentarnion A, pentarnion B) {
    return A*p + B*(p*p);
}
pentarnion p = 0.2 + 0.3*i - 0.1*j + 0.4*k + 0.15*ell;
pentarnion A = 1.0 + i;
pentarnion B = 0.2 - 0.1*ell;
options fixed = options(parameters=options(A=A, B=B));
matrix Jstate = jacobian(field, p, fixed);
matrix Jparameter = jacobian(field, p, options(parameters=options(A=A,B=B),wrt="A"));
display("Five-coordinate state Jacobian:", Jstate);
display("Five-coordinate parameter Jacobian for A:", Jparameter);
record checked = derivative_check(field,p,fixed);
assert(checked.passed);

% A real objective of a whole Pentarnion has a five-real-coordinate Hessian.
native function real energy(pentarnion p) {
    return norm2(p);
}
matrix g = gradient(energy,p);
matrix H = hessian(energy,p);
matrix Hv = hvp(energy,p,[1.0,2.0,3.0,4.0,5.0]);
display("Gradient of norm2(p):",g);
display("Hessian of norm2(p):",H);
display("Hessian-vector product:",Hv);
assert(H==2.0*eye(5));
assert(Hv==[2.0;4.0;6.0;8.0;10.0]);

% Whole-element one-element calculus, including a smooth zero-pure-part limit.
native function pentarnion whole_exp(pentarnion p) {
    return exp(p);
}
matrix at_real = jacobian(whole_exp, 0.2 + 0.0*i);
record exp_check = derivative_check(whole_exp,p,options(order=2));
display("Whole exp(p) Hessian verification:",exp_check);
assert(exp_check.passed);
assert(abs(at_real(1,1)-exp(0.2))<1e-12);
