clc;
clear;
native function tensor<2,1,1> cubes(tensor<2,1,1> x){return x.*x.*x;}
native function real energy(tensor<2,1,1> x){return x(1)^3+x(2)^3;}
native function tensor<2,1,1> residual(tensor<2,1,1> x){
    return x-tensor([1,-2],[2,1,1]);
}
native function real objective(tensor<2,1,1> x){
    return (x(1)-1)^2+(x(2)+2)^2;
}
tensor<2,1,1> point=tensor([2,3],[2,1,1]);
tensor<2,1,1> direction=tensor(1.0,[2,1,1]);
display("Native tensor Jacobian: ",jacobian(cubes,point));
display("Native energy Hessian: ",hessian(energy,point));
display("Hessian-vector product: ",hvp(energy,point,direction));
record third_result=thirdderivative(cubes,point,direction,direction,direction);
display("Analytic third directional derivative: ",third_result.third);
record root_result=rootsolve(residual,point);
record fit_result=lsqfit(residual,point);
record optimum=minimize(objective,point,options(algorithm="lbfgs"));
display("Root coefficients, retaining tensor rank/entry type: ",tensorvalues(root_result.x));
display("Least-square fitted coefficients: ",tensorvalues(fit_result.x));
display("Native optimized coefficients: ",tensorvalues(optimum.x));
display("Returned tensor descriptor: ",tensorinfo(optimum.x));
