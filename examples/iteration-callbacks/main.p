use_algebra("A_gamma",gamma=1);
clc;
clear;
close("all");
% OutputFcn is an observation callback, separate from the native objective.
% It runs on Studio's existing worker and may plot, display or request clean stop.
function integer monitor(matrix x,record optimValues=iterationinfo(),string state="iter") {
    if(optimValues.objective_available){
        figure(43,"Name","Live optimization","NumberTitle","off");
        subplot(1,2,1);
        plot(optimValues.history(:,2),optimValues.history(:,3));
        title("Objective history");xlabel("Function evaluations");
        subplot(1,2,2);
        matrix coordinates=1:numel(x);
        plot(coordinates,x);
        title("Current real coordinates");
    }
    if(state=="iter"&&optimValues.funccount>=300){return 1;}
    return 0;
}
native function real objective(real x,real y) {
    return 100.0*(y-x*x)*(y-x*x)+(1.0-x)*(1.0-x);
}
record fit=minimize(objective,[-1.2,1.0],
    options(algorithm="nelder_mead",max_evaluations=1000,
            OutputFcn=monitor,OutputEvery=5));
display("Status: ",fit.status,"; candidate: ",fit.x,"; objective: ",fit.objective);
% The same monitor contract works with lsqfit/lsqnonlin/curvefit/odefit,
% root/rootsolve/equilibrium/fixedpoint and every evolve algorithm.
