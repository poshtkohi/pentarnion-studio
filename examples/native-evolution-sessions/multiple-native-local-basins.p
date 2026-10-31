native function real wells(real x,real y){return (x*x-1)^2+0.2*(y-0.5)^2;}
record search=evolve(wells,[0,0],options(lower=-2.0,upper=2.0,
    population=48,max_generations=8,seed=7,islands=2,
    local_refinement=options(starts=6,separation=0.2,algorithm="bobyqa")));
display("Selected basin seeds: ",search.refinement.seed_x);
display("Native local solutions: ",search.refinement.x);
display("Local objective values: ",search.refinement.objective);
display("Best retained solution: ",search.x);
% Finding these wells is numerical evidence, not a proof that all basins exist.
