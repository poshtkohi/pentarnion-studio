Run main.p. The ordinary DSL monitor receives current real-coordinate row x,
optimValues metadata and state (init/iter/done). It draws convergence history and
the current point; return 1 requests a clean solver stop. Objective/RHS/residual and
constraint evaluation remains cached native code. NLopt/roots report evaluation
events, Ceres reports iterations, and evolutionary methods report generations;
inspect optimValues.event and availability flags rather than inferring semantics.
OutputEvery controls observation frequency; init/done always run. Current examples
may converge before the callback's 300-evaluation threshold, which is also valid.
