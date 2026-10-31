use_algebra("A_gamma",gamma=1);
clc;clear;close("all");
% Real R2 elliptic PDE: -Laplacian(u)=f, zero Dirichlet boundary.
% Full continuous H1 P4 nodes are retained separately from plot sampling.
native function real forcing(real x,real y,real amplitude){
    return 2*amplitude*(x*(1-x)+y*(1-y));
}
native function real exact(real x,real y,real amplitude){
    return amplitude*x*(1-x)*y*(1-y);
}
record M=trimesh([0,0;1,0;1,1;0,1;0.43,0.56],
    [1,2,5;2,3,5;3,4,5;4,1,5],
    options(boundary_tags=[1,2,1;2,3,2;3,4,3;4,1,4]));
tic;
record s=femsolve(M,@forcing,options(order=4,exact=@exact,
    parameters=options(amplitude=1),visualization_refinement=8));
toc;
display("Backend: ",s.backend,"; converged: ",s.converged);
display("Full nodal DOFs: ",s.dofs,"; relative algebraic residual: ",s.relative_residual);
display("Manufactured L2 error: ",s.l2_error);
figure(1,"Name","Real high-order FEM","NumberTitle","off");
femplot(s);title("Unstructured H1 P4 quartic solution");
xlabel("x");ylabel("y");zlabel("u(x,y)");
% Parameters pass at runtime; changing amplitude reuses the compiled callbacks.
record twice=femsolve(M,@forcing,options(order=4,exact=@exact,
    parameters=options(amplitude=2)));
display("Runtime-parameter callback cache hit: ",twice.cache_hit);
display("Linearity check: ",norm(twice.dof_values-2*s.dof_values));
