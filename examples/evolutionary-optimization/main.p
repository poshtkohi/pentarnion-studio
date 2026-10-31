use_algebra("A_gamma", gamma=1);
clc;
clear;
close("all");

% All objective evaluations execute cached native DLL/SO code.
native function real bowl(real x, real y, real a) {
    return (x-a)^2 + (y+0.5)^2;
}

tic;
record de = evolve(bowl,[0.9,0.8],options(algorithm="jde",
    parameters=options(a=0.25),lower=-1.0,upper=1.0,
    seed=42,population=40,max_generations=180));
record ide = evolve(bowl,[0.9,0.8],options(algorithm="ide",
    parameters=options(a=0.25),lower=-1.0,upper=1.0,
    seed=42,population=40,max_generations=180));
record cma = evolve(bowl,[0.9,0.8],options(algorithm="cmaes",
    parameters=options(a=0.25),lower=-1.0,upper=1.0,
    seed=42,population=40,max_generations=180,restarts=1));
record ga = evolve(bowl,[0.9,0.8],options(algorithm="ga",
    parameters=options(a=0.25),lower=-1.0,upper=1.0,
    seed=42,population=40,max_generations=180));
toc;

assert(de.objective<1e-5);
assert(ide.objective<1e-5);
assert(cma.objective<1e-5);
assert(ga.objective<1e-5);
display("Known optimum [0.25,-0.5]; jDE:",de.x);
display("iDE:",ide.x);
display("CMA-ES:",cma.x);
display("Elitist real-coded GA:",ga.x);
display("One unchanged objective body is reused:",ga.cache_hit);

% Change a runtime parameter without rebuilding the objective body.
record changed = evolve(bowl,[0.9,0.8],options(algorithm="jde",
    parameters=options(a=0.5),lower=-1.0,upper=1.0,
    seed=42,population=40,max_generations=180));
assert(changed.cache_hit);
assert(changed.cache_key==de.cache_key);
assert(abs(changed.x(1)-0.5)<0.004);

% Optional global-to-local refinement is explicit and inspectable.
record refined = minimize(bowl,de.x,options(parameters=options(a=0.25),
    algorithm="lbfgs",lower=-1.0,upper=1.0));
assert(norm(refined.x-[0.25,-0.5])<1e-6);
display("Local refinement:",refined.x);

figure(21,"Name","Evolutionary optimization convergence","NumberTitle","off");
subplot(2,2,1);plot(de.history(:,2),de.history(:,3));
title("Self-adaptive DE (jDE)");xlabel("Native objective evaluations");ylabel("Best objective");
subplot(2,2,2);plot(ide.history(:,2),ide.history(:,3));
title("Self-adaptive DE (iDE)");xlabel("Native objective evaluations");ylabel("Best objective");
subplot(2,2,3);plot(cma.history(:,2),cma.history(:,3));
title("CMA-ES with bounded coordinates");xlabel("Native objective evaluations");ylabel("Best objective");
subplot(2,2,4);plot(ga.history(:,2),ga.history(:,3));
title("Elitist real-coded genetic algorithm");xlabel("Native objective evaluations");ylabel("Best objective");

% Every x in [0,2] is nondominated for (x^2,(x-2)^2).
native function [real first, real second] tradeoff(real x) {
    return x*x, (x-2.0)^2;
}
record pareto = evolve(tradeoff,1.0,options(algorithm="nsga2",
    lower=0.0,upper=2.0,seed=42,population=40,max_generations=60));
assert(size(pareto.pareto_f,2)==2);
assert(size(pareto.pareto_f,1)>=30);
display("NSGA-II nondominated samples:",size(pareto.pareto_f,1));
display("Finite search termination:",pareto.status);
figure(22,"Name","NSGA-II Pareto tradeoff","NumberTitle","off");
plot(pareto.pareto_f(:,1),pareto.pareto_f(:,2));
title("Nondominated objective tradeoff");xlabel("$x^2$");ylabel("$(x-2)^2$");
tilefigures("studio",1,2);
