use_algebra("A_gamma",gamma=1);
clc;clear;close("all");
% Smooth manufactured solution. P1 L2 errors should approach O(h^2).
native function real rhs(real x,real y){
    return 19.739208802178716*sin(3.141592653589793*x)*sin(3.141592653589793*y);
}
native function real exact(real x,real y){
    return sin(3.141592653589793*x)*sin(3.141592653589793*y);
}
record M=trimesh([0,0;1,0;1,1;0,1;0.43,0.56],
    [1,2,5;2,3,5;3,4,5;4,1,5],
    options(boundary_tags=[1,2,1;2,3,2;3,4,3;4,1,4]));
record medium=meshrefine(M,options(levels=2));
record fine=meshrefine(M,options(levels=3));
record a=femsolve(medium,@rhs,options(order=1,exact=@exact));
record b=femsolve(fine,@rhs,options(order=1,exact=@exact));
display("P1 medium/fine L2 errors: ",a.l2_error,b.l2_error);
display("P1 error reduction: ",a.l2_error/b.l2_error);
record c=femsolve(medium,@rhs,options(order=2,exact=@exact));
display("P2 on the same medium mesh, L2 error: ",c.l2_error);
figure(1,"Name","FEM convergence","NumberTitle","off");
subplot(1,2,1);femplot(a);title("P1 medium mesh");
subplot(1,2,2);femplot(b);title("P1 refined mesh");
