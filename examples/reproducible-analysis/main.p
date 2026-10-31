use_algebra("A_gamma", gamma=1);
clc;
clear;
matrix data=[1,2,3,4,5,6,7,8];
record result=bootstrap(data,options(replicates=200,seed=2026));
record analysis=analysisrecord("Reproducible bootstrap demonstration",
    options(observations=data,bootstrap=result),
    options(source="examples/category19/main.p",seed=2026,
            assumptions="Independent identically distributed observations"));
saveanalysis("analysis-example.penta-analysis",analysis);
record restored=loadanalysis("analysis-example.penta-analysis",analysis);
assert(restored.data_hash==analysis.data_hash);
assert(norm(restored.data.bootstrap.estimates-result.estimates)==0);
display("Analysis data SHA-256: ",restored.data_hash);
display("Restored seed and interval: ",restored.data.bootstrap.seed,
    restored.data.bootstrap.lower,restored.data.bootstrap.upper);
% The safe data file is JSON with version/algebra/schema/hash checks; no code runs.
