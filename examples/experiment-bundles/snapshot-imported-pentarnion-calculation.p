use_algebra("A_gamma",gamma=1);
clc;clear;
import "quadratic-model.p" as model;
pentarnion left=(i*j)*ell;
pentarnion right=i*(j*ell);
record values=options(left=left,right=right,energy=model.energy(3,2),
    samples=[1,2;3,4]);
writematrix(values.samples,"selected-samples.csv");
record study=researchrecord("Ordered products with an imported model",values,
    options(bracketing="preserved",initial_displacement=3,stiffness=2));
saveexperiment("ordered-study.pexp.json",study,
    options(name="Ordered algebra and imported model",overwrite=1,
      artifacts=options(selected_table="selected-samples.csv")));
record loaded=loadexperiment("ordered-study.pexp.json",study);
assert(norm2(loaded.data.left-left)<1e-14);
display("Preserved left/right products: ",loaded.data.left,loaded.data.right);
record info=experimentinfo("ordered-study.pexp.json");
display("Source snapshots / external artifact references: ",info.source_count,info.external_artifact_count);
display("Contains context-bearing Pentarnion data: ",info.contains_pentarnion);
% Keep selected-samples.csv alongside the bundle: its name/hash are referenced,
% while artifact binary bytes are not embedded. Bundle loading executes nothing.
