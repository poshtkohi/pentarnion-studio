% Equivalent to the default real Map Explorer controls.
% Open the Map Explorer dock for expression/parameter entry without scripting.
use_algebra("A_gamma",gamma=1);
clear mapexplorer_fn mapexplorer_result;
native function real mapexplorer_fn(real x,real r){return r*x*(1-x);}
record mapexplorer_result=mapiterate(mapexplorer_fn,0.2,
    options(parameters=options(r=3.7),iterations=500,transient=100,stride=1,
            escape_radius=1e6,max_wall_seconds=60));
assert(mapexplorer_result.samples==401);
display(mapexplorer_result);
figure(20,"Name","Map Explorer","NumberTitle","off");
clf;
plot(mapexplorer_result.n,mapexplorer_result.states(:,1));
title("Real map orbit");xlabel("Iteration");ylabel("x");
caption("Finite samples do not prove periodicity, boundedness or chaos.");
