clc;
clear;
native function real L(real t,matrix<2,1> q,matrix<2,1> v) {
    return 0.5*(v(1)^2+v(2)^2);
}
native function real constraint(real t,matrix<2,1> q,matrix<2,1> v) {
    return v(2)-q(1);
}
record result = mechanicsdae(L,constraint,[0;0],[1;0],
    options(constraint_mode="velocity",t1=1,sample_step=0.01,
        rtol=1e-9,atol=1e-11));
display("d'Alembert velocity constraint and work:",result);
figure(1,"Name","Velocity constraints and reaction work","NumberTitle","off");
subplot(2,2,1);plot(result.q(:,1),result.q(:,2));title("$q_1=t, q_2=t^2/2$");
subplot(2,2,2);plot(result.t,result.constraints);title("Constraint residual");
subplot(2,2,3);plot(result.t,result.constraint_power);title("Reaction power");
subplot(2,2,4);plot(result.t,result.energy_balance);title("Energy balance");
