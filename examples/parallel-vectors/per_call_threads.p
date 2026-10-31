clc;
clear;

% threads(n) is a session default; each native call may choose its own maximum.
threads(4);
matrix x = linspace(0.0,1.0,40000);
matrix serial = sin(x,options(threads=1));
record one = parallel_info();
matrix parallel = sin(x,options(threads=2));
record two = parallel_info();
display("Session default:",threads());
display("Serial call report:",one);
display("Two-worker maximum report:",two);
assert(threads()==4);
assert(one.requested==4 && one.last_requested==1 && one.threads_used==1);
assert(two.requested==4 && two.last_requested==2 && two.threads_used<=2);
assert(serial==parallel);

% Reductions and native entrywise functions accept the same final options record.
real magnitude = norm(parallel,options(threads=1));
real inner = dot(x,parallel,options(threads=2));
matrix stable_exp = expm1(x,options(threads=2));
matrix column = transpose(x,options(threads=1));
display("Norm:",magnitude," dot:",inner);
display("expm1(1):",stable_exp(end));
assert(abs(stable_exp(end)-(exp(1.0)-1.0))<1e-14);
assert(size(column,1)==40000 && size(column,2)==1);
assert(threads()==4);
threads(0);
