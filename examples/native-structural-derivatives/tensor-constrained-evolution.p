clc;
clear;
% A full rank-three native state with two retained real coordinates.
native function tensor<2,1,1> residual(real t,tensor<2,1,1> y,tensor<2,1,1> yp){
    tensor<2,1,1> result=tensor(0.0,[2,1,1]);
    result(1)=yp(1)+y(1);
    result(2)=y(2)-y(1)*y(1);
    return result;
}
tensor<2,1,1> point=tensor([1,0],[2,1,1]);
tensor<2,1,1> guess=tensor(0.0,[2,1,1]);
record initial=daeinitialize(residual,0.0,point,guess);
display("Consistent tensor coefficients: ",initial.consistent_initial);
display("Complete initial derivatives: ",initial.consistent_derivative);
display("Automatically identified algebraic coordinate mask: ",initial.algebraic);
record solved=daesolve(residual,initial.consistent_initial,initial.consistent_derivative,
    options(algebraic=initial.algebraic,t1=2,sample_step=0.01));
figure(1,"Name","Native tensor DAE with a nonlinear constraint");
subplot(2,1,1);plot(solved.t,solved.values(:,1));xlabel("t");ylabel("x(t)");
subplot(2,1,2);plot(solved.t,solved.values(:,2));xlabel("t");ylabel("z(t)");
caption("The compiler emits the residual Jacobian; z=x squared and x prime=-x.");
