clc;
clear;
record A = complexarray([2,1;0.5,3],[1,-1;2,-0.5],[2,2]);
record target = complexarray([1,0.3;-0.2,2],[0.5,-0.4;0.1,0.2],[2,2]);
record B = ndmatmul(A,target);
record solved = ndsolve(A,B);
assert(norm(ndvalues(solved)-ndvalues(target),"fro") < 1e-11);
display("Complete real/imaginary solution coordinates:",ndvalues(solved));

record inverse = ndinv(A);
display("A times its checked inverse:",ndvalues(ndmatmul(A,inverse)));

record factors = ndlu(A);
record left = ndmatmul(ndmatmul(factors.P,A),factors.Q);
record right = ndmatmul(factors.L,factors.U);
assert(norm(ndvalues(left)-ndvalues(right),"fro") < 1e-11);
display("LU relation:",factors.relation);
display("U real/imaginary coordinates:",ndvalues(factors.U));
