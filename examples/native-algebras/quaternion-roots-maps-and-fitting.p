clc;
clear;
algebra H=builtin_algebra("quaternion");
element target=1+H.i-2*H.j+0.25*H.k;

% Entire residual/map/objective bodies compile to cached native DLL/SO files.
% All four quaternion coefficients remain unknowns and enter analytic AD.
native function element<H> residual(element<H> p,element<H> target){
    return p-target;
}
native function element<H> contraction(element<H> p,real rho,element<H> target){
    return rho*p+(1.0-rho)*target;
}
native function real energy(element<H> p,element<H> target){
    return norm2(p-target);
}

record root_result=rootsolve(residual,0.0,options(parameters=options(target=target)));
record fitted=lsqfit(residual,0.0,options(parameters=options(target=target)));
record minimum=minimize(energy,0.0,options(parameters=options(target=target)));
record fixed=fixedpoint(contraction,0.0,options(parameters=options(rho=0.5,target=target)));
record orbit=mapiterate(contraction,0.0,options(parameters=options(rho=0.5,target=target),iterations=25));

assert(root_result.converged);assert(fitted.converged);assert(minimum.converged);assert(fixed.converged);
assert(algebra_norm(orbit.x-target)<1e-6);
display("Expected quaternion:",target);
display("Native nonlinear root:",root_result.x);
display("Native least-squares fit:",fitted.x);
display("Native minimum:",minimum.x);
display("Native fixed point:",fixed.x);

figure(1,"Name","Quaternion map convergence","NumberTitle","off");
subplot(2,2,1);plot(orbit.n,orbit.states(:,1));title("Scalar coefficient");xlabel("Iteration");
subplot(2,2,2);plot(orbit.n,orbit.states(:,2));title("$i$ coefficient");xlabel("Iteration");
subplot(2,2,3);plot(orbit.n,orbit.states(:,3));title("$j$ coefficient");xlabel("Iteration");
subplot(2,2,4);plot(orbit.n,orbit.states(:,4));title("$k$ coefficient");xlabel("Iteration");
