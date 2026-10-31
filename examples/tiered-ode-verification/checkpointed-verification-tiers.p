% Rerunnable example: overwrite_checkpoint is an explicit choice in this script.
% Completed verification tiers are saved atomically; interrupted tiers restart.
clc;
ode decay(t,real x) {x'=-x;}
record screen=discover(decay,[1;2],options(t1=0.5,sample_step=0.05));
record evidence=verifyexperiment(decay,screen,[1,2],options(
    horizons=[1,2,4],checkpoint="verification-tiers.json",overwrite_checkpoint=1));
record resumed=verifyexperiment(decay,screen,[1,2],options(
    horizons=[1,2,4],checkpoint="verification-tiers.json",resume=1));
assert(resumed.restored_tiers==3);
assert(resumed.states==evidence.states);
display("Completed tiers reused:",resumed.restored_tiers);
display("Final verified state table:",resumed.states);
