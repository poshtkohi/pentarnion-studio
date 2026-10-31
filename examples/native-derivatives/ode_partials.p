use_algebra("A_gamma", gamma=1);
clc;
clear;
close("all");

% Differentiate an ODE right-hand side natively, without integrating its flow.
% States are the point; model parameters and time are passed as fixed values.
ode time_dependent(t,real y;real a) {
    y' = -a*y+t*y*y;
}
options cfg = options(parameters=options(a=3.0),time=0.5);
matrix J_state = jacobian(time_dependent,2.0,cfg);
matrix J_parameter = jacobian(time_dependent,2.0,
    options(parameters=options(a=3.0),time=0.5,wrt="a"));
matrix J_time = jacobian(time_dependent,2.0,
    options(parameters=options(a=3.0),time=0.5,wrt="t"));
assert(abs(J_state(1,1)+1)<1e-12);
assert(abs(J_parameter(1,1)+2)<1e-12);
assert(abs(J_time(1,1)-4)<1e-12);
display("RHS state Jacobian dF/dy, expected -1:",J_state);
display("RHS parameter Jacobian dF/da, expected -2:",J_parameter);
display("RHS time partial dF/dt, expected 4:",J_time);

% One scalar-real RHS also supports Hessians and Hessian-vector products.
% Flattened input coordinates are [y,a,t].
matrix H = hessian(time_dependent,2.0,
    options(parameters=options(a=3.0),time=0.5,wrt=[1,2,3]));
matrix Hv = hvp(time_dependent,2.0,[1,2,3],
    options(parameters=options(a=3.0),time=0.5,wrt=[1,2,3]));
assert(norm(H-[1,-1,4;-1,0,0;4,0,0],"fro")<1e-12);
assert(norm(Hv-[11;-1;4])<1e-12);
display("RHS Hessian with respect to [y,a,t]:",H);
display("Hessian times [1,2,3], expected [11;-1;4]:",Hv);

record check_half = derivative_check(time_dependent,2.0,
    options(parameters=options(a=3.0),time=0.5,wrt=[1,2,3]));
record check_one = derivative_check(time_dependent,2.0,
    options(parameters=options(a=3.0),time=1.0,wrt=[1,2,3],order=2));
assert(check_half.passed && check_one.passed);
assert(check_half.cache_key==check_one.cache_key);
display("Changed time and derivative request reuse native callback:",check_one);

% Pentarnion states/parameters give five real coefficient coordinates each.
ode linear_pentarnion(t,pentarnion p;pentarnion A) {
    p' = A*p+p*A;
}
matrix J_pentarnion = jacobian(linear_pentarnion,2.0,
    options(parameters=options(A=3.0)));
matrix J_A = jacobian(linear_pentarnion,2.0,
    options(parameters=options(A=3.0),wrt="A"));
assert(norm(J_pentarnion-6*eye(5),"fro")<1e-12);
assert(norm(J_A-4*eye(5),"fro")<1e-12);
display("Five-coordinate state Jacobian, expected 6*I:",J_pentarnion);
display("Five-coordinate parameter Jacobian, expected 4*I:",J_A);

ode bracketed(t,pentarnion p;pentarnion A,pentarnion B) {
    p' = A*p+B*(p*p);
}
record bracket_check = derivative_check(bracketed,1+i-j+ell,
    options(parameters=options(A=1.0+i,B=0.5+j),time=0.25));
assert(bracket_check.passed);
display("Bracket-preserving nonlinear Pentarnion RHS check:",bracket_check);

% Plot actual native partial evaluations over a state sweep.
vector states = linspace(-1,3,161);
vector state_partials = zeros(1,length(states));
vector parameter_partials = zeros(1,length(states));
for(n=1:length(states)) {
    matrix sampled_state = jacobian(time_dependent,states(n),cfg);
    matrix sampled_parameter = jacobian(time_dependent,states(n),
        options(parameters=options(a=3.0),time=0.5,wrt="a"));
    state_partials(n) = sampled_state(1,1);
    parameter_partials(n) = sampled_parameter(1,1);
}
figure(2,"Name","Native ODE RHS partials","NumberTitle","off");
subplot(1,2,1);plot(states,state_partials);
title("State partial $\\partial F/\\partial y$");xlabel("$y$");ylabel("$F_y$");
subplot(1,2,2);plot(states,parameter_partials);
title("Parameter partial $\\partial F/\\partial a$");xlabel("$y$");ylabel("$F_a$");
