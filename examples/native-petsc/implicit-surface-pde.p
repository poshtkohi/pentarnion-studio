% Fully native reaction/initial callbacks; PETSc solves generated sparse Newton
% systems. The same interface also accepts structured1D/2D FD domains.
native function real reaction(real u,real t,real x,real y,real z) {
    return -2*u;
}
native function real initial(real t,real x,real y,real z) {
    return x+y;
}
record mesh=meshplane([0,1],[0,1],options(nx=9,ny=9));
record field=pdeimplicit(reaction,mesh,initial,options(
    t1=.1,time_step=.01,sample_step=.01,adaptive=0,
    linear_backend="petsc_ilu",threads=2));
display("Implicit surface PDE / PETSc linear iterations: ",field.status,field.linear_iterations);
display("Shared native callback/Jacobian cache: ",field.cache_keys);
matrix final=pdeimplicitslice(field,length(field.t));
figure(1,"Name","PETSc implicit surface PDE","NumberTitle","off");
matrix index=linspace(1,field.nodes,field.nodes);
plot(index,final);title("Final surface field coefficients");
