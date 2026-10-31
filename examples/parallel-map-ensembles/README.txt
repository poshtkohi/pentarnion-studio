PARALLEL NATIVE MAP ENSEMBLES

Run main.p. One row of the initial real-coordinate matrix is one independent task.
Callbacks may return a real/Pentarnion scalar or typed tuple matching that row's
state coordinates. Whole Pentarnion states occupy five adjacent real columns.
The shared CPU budget controls outer workers and forces nested numerical work
to one slot; threads is a maximum, and actual workers are reported separately.

Expected results: serial/parallel contraction outputs are exactly equal on this
build/platform; the input zero fails reciprocal evaluation while rows1 and3
succeed; the logistic parameter remains unchanged in each corresponding row.
The second plot shows one final sample for each parameter, not a full orbit or
true branch continuation. Use mapdiagram for its existing sampled-orbit contract.

The native callback body is compiled once, with no interpreted per-evaluation
fallback. New runtime fixed parameters or initial values reuse the same cache key.
Each result retains row identities, completion counts, failure messages and
separate escaped/success masks. Failed rows retain their last valid finite state.

This bounded ensemble is deterministic and does not introduce random callbacks.
General parfor syntax, parallel ODE solves, per-run random streams, incremental
progress/checkpoint-resume and automatic basin classification remain future work.
