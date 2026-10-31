use_algebra("A_gamma", gamma=1);
clc;
clear;

% A per-call thread limit overrides, without changing, the session default.
threads(4);
rng(2026);
matrix A=rand(128,128);
matrix B=rand(128,128);
% Warm the optional module and persistent workers before comparing timings.
matrix warm=matmul(A,B,options(threads=4));
clear warm;
tic;
matrix serial=matmul(A,B,options(threads=1));
real serial_seconds=toc();
record serial_workers=parallel_info();
tic;
matrix parallel=matmul(A,B,options(threads=4));
real parallel_seconds=toc();
record parallel_workers=parallel_info();
assert(norm(serial-parallel,"fro")<1e-10);
assert(threads()==4);
display("Serial/parallel seconds:",serial_seconds,parallel_seconds);
display("Serial worker report:",serial_workers);
display("Parallel worker report:",parallel_workers);

% Ordinary matrix multiplication uses the same native backend and default limit.
matrix C=A*B;
assert(norm(C-parallel,"fro")<1e-10);
matrix row=[1,2,3];
matrix column=[4;5;6];
display("Row times column (32):",row*column);
[matrix first,matrix second]=batchmul((A,A),(eye(128),2*eye(128)),options(threads=2));
assert(norm(first-A,"fro")<1e-12);
assert(norm(second-2*A,"fro")<1e-12);
display("Batch identity products passed.");

matrix field=[1,2,3;4,5,6];
display("Column sums [5,7,9]:",sum(field,1));
display("Row means [2;5]:",mean(field,2));
display("Transposed field:",transpose(field));
display("Entrywise sine:",sin(field));

% Pentarnion entry products keep their operand order and explicit grouping.
matrix P=[1+0.2*i-0.3*j+0.4*k+0.5*ell];
matrix Q=[-0.1+0.4*i+0.2*j-0.5*k+0.1*ell];
matrix R=[0.2-0.2*i+0.6*j+0.3*k-0.4*ell];
display("(P*Q)*R:",(P*Q)*R);
display("P*(Q*R):",P*(Q*R));
matrix matrix_associator=(P*Q)*R-P*(Q*R);
display("Matrix associator:",matrix_associator);
real difference=0;
for(c=0:4){difference=difference+abs(coeff(matrix_associator(1),c));}
assert(difference>1e-8);
threads(0);
