% u'=-2u, z'=x, 0=z-sin(omega*t).
% Hence z=sin(omega*t), x=omega*cos(omega*t): differentiation index2.
native function [real first,real second,real third] forcing_term(real t,real omega){
    return 0.0,0.0,-sin(omega*t);
}
matrix descriptor=[1,0,0;0,0,1;0,0,0];
matrix linear=[-2,0,0;0,1,0;0,0,1];
record solved=lineardaesolve(descriptor,linear,forcing_term,[1,0,0],
    options(parameters=options(omega=1.3),t0=0.2,t1=5,sample_step=0.01));
display("Recovered index: ",solved.index);
display("Consistent initial state: ",solved.consistent_initial);
display("Consistent derivative: ",solved.consistent_derivative);
display("Original scaled residual: ",solved.maximum_scaled_residual);
figure(1,"Name","Index-two native descriptor system","NumberTitle","off");
subplot(3,1,1);plot(solved.t,solved.values(:,1));title("Finite decay coordinate");
subplot(3,1,2);plot(solved.t,solved.values(:,2));title("Hidden constraint: x=omega cos(omega t)");
subplot(3,1,3);plot(solved.t,solved.values(:,3));title("Algebraic coordinate: z=sin(omega t)");xlabel("t");
