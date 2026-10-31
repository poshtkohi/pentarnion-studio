native function pentarnion reaction(pentarnion u,real t,real x,real y,real z) {
    return -.5*u;
}
record mesh=meshplane([0,1],[0,1],options(nx=9,ny=9));
record field=pdeimplicit(reaction,mesh,1+i,options(
    diffusion=.1,t1=.2,time_step=.01,sample_step=.02,adaptive=0,threads=2));
matrix scalar_history=pdeimplicitcoeff(field,0);
matrix i_history=pdeimplicitcoeff(field,1);
display("Complete real-coordinate width: ",field.components);
display("Real/i component agreement: ",max(abs(scalar_history-i_history)));
display("Jacobian/parallel calls: ",field.jacobian_evaluations,field.parallel_calls);
figure(1,"Name","Implicit Pentarnion P1 surface reaction","NumberTitle","off");
plot(field.t,scalar_history(:,1));title("Lumped P1 surface field with implicit reaction");
