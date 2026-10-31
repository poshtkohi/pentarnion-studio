clc;
% Noise is an explicit runtime argument of a pure compiled map, not a mutable
% global generator. Seed/task streams are independent of worker scheduling.
native function real noisy_map(real x,real noise,real rho){
    return rho*x+0.1*(noise-0.5);
}
native function real pause_after_first_batch(real completed,real total){
    return completed>=2;
}
record paused=ensemble(noisy_map,[0;1;2;3;4;5],options(
    parameters=options(rho=0.8),iterations=50,threads=2,batch_size=2,seed=42,
    random_parameter="noise",distribution="uniform",ProgressFcn=pause_after_first_batch,
    checkpoint="map-runs.json",overwrite_checkpoint=1));
assert(paused.user_stopped);assert(paused.completed==2);
record resumed=ensemble(noisy_map,[0;1;2;3;4;5],options(
    parameters=options(rho=0.8),iterations=50,threads=4,batch_size=3,seed=42,
    random_parameter="noise",distribution="uniform",checkpoint="map-runs.json",resume=1));
record serial=ensemble(noisy_map,[0;1;2;3;4;5],options(
    parameters=options(rho=0.8),iterations=50,threads=1,seed=42,
    random_parameter="noise",distribution="uniform"));
assert(resumed.states==serial.states);assert(resumed.restored==2);
display("Reused completed stochastic runs:",resumed.restored);
display("Serial/parallel reproducible final states:",resumed.states);
