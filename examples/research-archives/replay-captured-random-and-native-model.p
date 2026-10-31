rng(42);
matrix samples=rand(1,8);
native function real model(real x){return x*x+1;}
real value=model(samples(1));
record study=options(samples=samples,value=value);
savearchive("random-model.archive.json",study,
    options(name="Reproducible native model",workspace=options(samples=samples),overwrite=1));
% Loading the archive has no execution side effects.
record info=archiveinfo("random-model.archive.json");display(info);
% Replay is explicit; a separate output directory holds any produced files.
record replayed=replayexperiment("random-model.archive.json",
    options(output_directory="isolated-random-replay"));
display("Replay completed: ",replayed.ok);
display("Recreated variables: ",replayed.variables);
