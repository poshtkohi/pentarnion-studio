clc;
clear;
native function matrix<2,1> constraint_force(real t,matrix<2,1> y) {
    return [-y(1);y(2)-y(1)^2];
}
native function matrix<2,2> singular_mass(real t,matrix<2,1> y) {
    return [1,0;0,0];
}
record result = masssolve(constraint_force,singular_mass,[1;1],[-1;-2],
    options(t1=3,sample_step=0.025,rtol=1e-10,atol=1e-12));
assert(result.algebraic == [0,1]);
display("Automatically identified algebraic coordinates:",result.algebraic);
display("Index contract:",result.index_contract);
figure(1,"Name","Singular mass and algebraic constraint","NumberTitle","off");
subplot(2,1,1);plot(result.t,result.values(:,1));title("$x'= -x$");
subplot(2,1,2);plot(result.t,result.values(:,2));title("$z=x^2$");xlabel("$t$");
