use_algebra("A_gamma",gamma=1);
clc;
clear;
close("all");

ode stiff_node(t,real x;real alpha){x'=-alpha*x;}
record G=graphgen("path",2);
record s=networksolve(stiff_node,G,[1;3],
    options(parameters=options(alpha=1000),method="bdf",t1=0.01,
        sample_step=0.0001,diffusion=500,rtol=1e-9,atol=1e-12,
        krylov_dimension=10,threads=0));
matrix first=networkcoeff(s,1,1);
matrix second=networkcoeff(s,2,1);
assert(abs(first(end)-2*exp(-10)+exp(-20))<1e-9);
assert(abs(second(end)-2*exp(-10)-exp(-20))<1e-9);
display("Generated local Jacobians:",s.local_jacobian_evaluations);
display("Matrix-free J*v calls:",s.jacobian_vector_products);
display("SPGMR iterations and local block factorizations:",s.linear_iterations,s.block_factorizations);
display("Actual adaptive steps and largest accepted step:",s.steps,s.largest_step);

figure(1,"Name","Matrix-free native stiff graph dynamics","NumberTitle","off");
plot(s.t,first);hold("on");plot(s.t,second);hold("off");
title("Two independently known stiff exponential modes");xlabel("$t$");ylabel("$x$");
legend("node 1","node 2");
caption("CVODES BDF; generated local Jacobian blocks, SPGMR and block-Jacobi preconditioning.");
