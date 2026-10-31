% Category 4: parallel real vectors and one shared CPU thread budget.
clc;
clear;
threads(4);
display("Requested threads:",threads());

vector x=linspace(0,1,40000);
tic;
vector y=sin(x);
real elapsed=toc();
record sine_run=parallel_info();
display("Parallel sine elapsed seconds:",elapsed);
display("Sine worker report:",sine_run);
assert(sine_run.parallel_calls>0);
assert(sine_run.threads_used<=4);

vector z=axpy(2,x,y);
record axpy_run=parallel_info();
assert(abs(z(end)-(2+sin(1)))<1e-12);
display("AXPY last value:",z(end));
display("AXPY worker report:",axpy_run);

real inner=dot(x,y);
real magnitude=norm(z);
display("Real inner product:",inner);
display("Euclidean norm:",magnitude);

vector small=[1,2,3]+[4,5,6];
record small_run=parallel_info();
assert(small_run.threads_used==1);
display("Small work stays serial:",small);
threads(0);
display("Automatic thread selection restored:",threads());
