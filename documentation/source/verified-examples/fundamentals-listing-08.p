matrix A = [2,0;0,4];
vector b = [2;8];
matrix x = linsolve(A,b,options(threads=1));
display(x);               % [1; 2]
assert(norm(A*x-b) < 1e-12);
