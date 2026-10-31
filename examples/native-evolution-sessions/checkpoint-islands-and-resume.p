% Native objective plus deterministic complete-generation resume across workers.
native function real bowl(real x,real y,real target){
    return (x-target)^2+(y+0.5)^2;
}
function integer pause_after_eight(matrix x,record info=iterationinfo(),string state="iter"){
    return state=="iter"&&info.iteration>=8;
}
record paused=evolve(bowl,[0,0],options(parameters=options(target=0.25),
    algorithm="cmaes",lower=-1.0,upper=1.0,population=24,restarts=0,
    islands=2,migration_interval=3,max_generations=30,seed=42,threads=4,
    checkpoint="cma-islands.json",overwrite_checkpoint=1,OutputFcn=pause_after_eight));
display("Paused generation: ",paused.generations);
record resumed=evolve(bowl,[0,0],options(parameters=options(target=0.25),
    algorithm="cmaes",lower=-1.0,upper=1.0,population=24,restarts=0,
    islands=2,migration_interval=3,max_generations=30,seed=42,threads=2,
    checkpoint="cma-islands.json",resume=1));
display("Restored generation: ",resumed.restored_generation);
display("Best coordinates: ",resumed.x);
display("Best objective: ",resumed.objective);
display("Accepted ring migrants: ",resumed.migrations);
figure(1,"Name","Native island evolution","NumberTitle","off");
plot(resumed.history(:,1),resumed.history(:,3));
title("Best evaluated objective across native generations");
xlabel("Generation");ylabel("Objective");
