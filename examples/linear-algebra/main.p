clc;
clear;
matrix A=[4,1,0;1,3,1;0,1,2];
matrix b=[1;2;3];
options settings=options(method="cholesky",threads=4);
record solve=solve_report(A,b,settings);
assert(solve.converged);
display("Solution [2/9;1/9;13/9]:",solve.solution);
display("Linear solve diagnostics:",solve);
display("Determinant (18):",det(A));
matrix inverse=inv(A,options(threads=2));
assert(norm(A*inverse-eye(3),"fro")<1e-12);
display("Real inverse:",inverse);

record L=lu(A);
record Q=qr(A);
record S=svd(A);
matrix C=chol(A);
assert(norm(L.P*A-L.L*L.U,"fro")<1e-10);
assert(norm(A*Q.P-Q.Q*Q.R,"fro")<1e-10);
assert(norm(A-S.U*S.S*transpose(S.V),"fro")<1e-10);
assert(norm(A-C*transpose(C),"fro")<1e-10);
display("LU, pivoted QR, thin SVD and Cholesky reconstructions passed.");
display("Rank and reciprocal2-condition:",rank(A),rcond(A));

% Complex spectral results remain separate real/imaginary arrays.
matrix rotation=[0,-1;1,0];
record spectrum=eig(rotation);
display("Eigenvalue real parts [0;0]:",spectrum.values_real);
display("Eigenvalue imaginary parts (+1,-1 in an unspecified order):",spectrum.values_imag);
record form=schur(rotation);
assert(norm(rotation-form.Q*form.T*transpose(form.Q),"fro")<1e-12);

% A least-squares minimum is not necessarily an equation solution.
record candidate=solve_report([1,2;2,4],[1;0],options(method="svd"));
assert(!candidate.converged);
display("Inconsistent-system least-squares candidate:",candidate);
