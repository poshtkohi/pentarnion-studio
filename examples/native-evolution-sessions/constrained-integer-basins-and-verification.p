% Analytic lattice-constrained optimum: x=2,y=-1.5,objective=0.25.
native function real bowl(real x,real y){return (x-2)^2+(y+1)^2;}
native function real boundary(real x,real y){return x+y-0.5;}
native function real accurate_bowl(real x,real y){
    return (x-2)^2+(y+1)^2+0.01;
}
record search=evolve(bowl,[0,0],options(lower=-4.0,upper=4.0,
    algorithm="jde",population=32,max_generations=30,seed=42,islands=2,
    integer_coordinates=[1],inequality=boundary,constraint_tolerance=1e-8,
    local_refinement=options(starts=4,separation=0.15,algorithm="slsqp"),
    verification=options(objective=accurate_bowl,tolerance=1e-10,finalists=4)));
display("Native constrained result: ",search.x);
display("Original objective: ",search.objective);
display("Constraint violation: ",search.constraint_violation);
display("Local basin attempts: ",search.refinement.attempts);
display("Local statuses: ",search.refinement.statuses);
display("Supplied verification model: ",search.verification);
% The additive verification offset is deliberately simple and independently known.
% A research model can supply its own tighter native approximation here.
