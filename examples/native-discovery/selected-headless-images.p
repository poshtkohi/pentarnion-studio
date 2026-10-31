use_algebra("A_gamma",gamma=1);clc;clear;
ode oscillator(t,real x,real v;real stiffness){x'=v;v'=-stiffness*x;}
record experiment=discover(oscillator,[1,0;2,0],options(
    parameters=options(stiffness=searchset([1,2])),t1=10.0,
    sample_step=0.02,threads=4,max_runs=8));
% Opt-in native PNG export also works in penta-run without the IDE.
% Filenames and the catalogue/export ledger are created only by this command.
record images=exportexperiment(oscillator,experiment,options(
    directory="selected-images",prefix="oscillator",ids=[1,3],
    kinds="series,trajectory2,norm,summary",series_columns=[1,2],
    trajectory2_columns=[1,2],width=1000,height=700,dpi=150,
    maximum_images=7,maximum_bytes=67108864));
display("Written image count / export manifest:",images.written,images.manifest);
% Run again with resume=1 to validate existing images and skip completed exports.
% To make different plots, choose a new prefix/directory or explicit overwrite=1.
% All labels preserve their complete UTF-8 metadata. The portable native PNG
% fallback displays ASCII bitmap captions; Studio exports provide math typography.
