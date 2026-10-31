% u'=-2u, y2'=y1, y3'=y2, 0=y3-sin(omega*t).
native function [real first,real second,real third,real fourth] forcing_term(real t,real omega){
    return 0.0,0.0,0.0,-sin(omega*t);
}
matrix descriptor=[1,0,0,0;0,0,1,0;0,0,0,1;0,0,0,0];
matrix linear=[-2,0,0,0;0,1,0,0;0,0,1,0;0,0,0,1];
record solved=lineardaesolve(descriptor,linear,forcing_term,[1,0,0,0],
    options(parameters=options(omega=1.3),t0=0.2,t1=3,sample_step=0.02,
        sensitivity_parameters=[1],parameter_lower=0.5,parameter_upper=2.0));
display("Recovered index: ",solved.index);
display("Consistent y0 / yp0: ",solved.consistent_initial,solved.consistent_derivative);
display("Consistent initial parameter Jacobian: ",solved.initial_parameter_jacobian);
display("Consistent derivative parameter Jacobian: ",solved.initial_derivative_parameter_jacobian);
display("Sensitivity method: ",solved.sensitivity_method);
figure(1,"Name","Native index-three coordinates","NumberTitle","off");
subplot(2,1,1);plot(solved.t,solved.values(:,2));title("y1=-omega^2 sin(omega t)");
subplot(2,1,2);plot(solved.t,solved.sensitivities(:,4));title("Parameter sensitivity of y3");xlabel("t");
