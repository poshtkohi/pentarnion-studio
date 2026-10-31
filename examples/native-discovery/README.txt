NATIVE ODE DISCOVERY AND MATCHED FORCE EXPERIMENTS

main.p: existing ODE definitions, explicit finite/sampled domains, dry-run counts,
native shared-kernel parallel ODE experiments and selected-run replay/plotting.
catalogue-preflight.p:242 normalized nonzero signed ternary directions,58,564
ordered A/B pairs and117,128 matched paired-model runs before other factors.
matched-forces.p: small explicitly matched positive-strength sample for B*(q*q)
and (B*q)*q. The physical state includes all five q and all five v coordinates.
ordered-work.p: optional explicit work-integral augmentation, including both
ordered products and their commutator. Native regression checks the exact
constant-real-mass identity in a genuinely noncommuting test.

Candidate IDs are one-based and deterministic: gamma, Cartesian parameter
combination, initial row. The first declared parameter varies fastest. Runtime
parameters/options do not change the compiled equation structure. Context gamma
is immutable inside each integration and is stored with every run. Replay yields
real coordinate columns plus gamma metadata, avoiding workspace-context ambiguity.

Finite-time labels are evidence only: bounded_over_horizon,
equilibrium_candidate, recurrent_candidate, periodic_candidate, threshold_reached,
timeout, numerical_failure, inconclusive. This stage does not certify chaos,
global boundedness, fundamental periods or orbital stability. Use periodicorbit
and Lyapunov tools separately and study horizon/tolerance convergence.

Checkpoints atomically store versioned scientific inputs, native model/backend
identity, result integrity hash and completed-task ledger. A compatible resume
skips completed tasks; interrupted tasks restart from recorded initials. This is
not serialization of SUNDIALS's private mid-step memory. Choose a new path for a
different experiment or explicitly request overwrite. Output/timing/thread fields
are excluded from scientific serial/parallel comparisons.

Current limits:1..128 real state coordinates,100,000 tasks hard cap, explicit
run/result/work/time budgets; default max_runs4096 and256MiB result/working budget.
Domains require finite sets or explicit seeded uniform/linspace sampling. Search
is general ODE execution, not a general optimizer. Follow-up tiers, automated
Lyapunov verification, full catalogue exports and export-ledger resumption remain
separate acceptance items in WORK_TO_DO_2.txt.
