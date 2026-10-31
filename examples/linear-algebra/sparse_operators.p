clc;
clear;
integer n=300;
matrix center=1:n;
matrix lower=2:n;
matrix upper=1:n-1;
record A=sparse([center,lower,upper],[center,upper,lower],
    [2*ones(1,n),-ones(1,n-1),-ones(1,n-1)],n,n);
assert(nnz(A)==3*n-2);
matrix b=ones(n,3);
record direct=sparse_solve_report(A,b,options(method="sparselu",threads=3));
record workers=parallel_info();
assert(direct.converged);
assert(norm(sparsemul(A,direct.solution)-b,"fro")<1e-7);
display("Sparse nonzeros (898):",nnz(A));
display("Three independent RHS worker report:",workers);
display("Sparse direct method / residual:",direct.method,direct.relative_residual);
display("First / last solution entry:",direct.solution(1,1),direct.solution(end,1));

record iterative=sparse_solve_report(A,b,options(method="bicgstab",
    preconditioner="ilut",threads=3,tolerance=1e-10,max_iterations=2000,
    residual_tolerance=1e-9));
assert(iterative.converged);
assert(norm(sparsemul(A,iterative.solution)-b,"fro")<1e-6);
display("Iterative method / iterations / residual:",iterative.method,iterative.iterations,iterative.relative_residual);

record diagonal=sparse([1,1,2],[1,1,2],[2,3,7],2,2);
display("Duplicate triplets add to [5,0;0,7]:",full(diagonal));
record squared=sparsemul(diagonal,diagonal);
assert(full(squared)==[25,0;0,49]);
display("Sparse transpose:",full(sparsetranspose(diagonal)));
