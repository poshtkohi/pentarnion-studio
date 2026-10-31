% Capture scientific data and the exact current source as data-only JSON.
clc;clear;
matrix samples=[0,1;0.5,0.606530659712633;1,0.367879441171442];
record results=options(samples=samples,status="completed",initial_value=1);
record study=researchrecord("Real exponential samples",results,
    options(seed=42,reference="exp(-t)",numerical_unit_system="seconds"));
saveexperiment("real-study.pexp.json",study,
    options(name="Real exponential study",overwrite=1));
record restored=loadexperiment("real-study.pexp.json",study);
assert(restored.data.samples==samples);
record info=experimentinfo("real-study.pexp.json");
display("Integrity / context compatibility: ",info.integrity_valid,info.compatible);
display("Scientific identity: ",info.scientific_hash);
display("Exact current source hash: ",info.main_source_hash);
display("Workspace history included: ",info.workspace_history_included);
% Source retrieval returns a string. It does not execute the captured program.
string captured_source=experimentsource("real-study.pexp.json");
assert(captured_source!="");
