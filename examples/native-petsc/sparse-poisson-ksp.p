% Geometry-independent sparse operators cover FD and assembled FEM systems.
integer n=40;
matrix diagonal=ones(n,1);
record A=sparse(diag(2*diagonal)-diag(ones(n-1,1),1)-diag(ones(n-1,1),-1));
matrix exact=sin(linspace(1,n,n)*3.14159265358979323846/(n+1));
matrix b=(2-2*cos(3.14159265358979323846/(n+1)))*exact;
record report=petscsolve(A,b,options(method="cg",preconditioner="jacobi",threads=2));
display("PETSc residual / solution error: ",report.residual_norm,norm(report.x-exact',"fro"));
display("Status and shared-pool workers used: ",report.status,report.threads_used);
figure(1,"Name","Native PETSc Poisson solve","NumberTitle","off");
matrix index=linspace(1,n,n);
plot(index,report.x);title("Sparse Dirichlet Poisson operator");
