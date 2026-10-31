Native inference and parallel independent refits (roadmap6B/C and1A initial adapter)

Examples: parallel_multistart.p, profile_known_noise.p,
bootstrap_fitted_mean_bands.p, moving_block_refits.p, unidentifiable_profile.p.
Run checkpoint_bootstrap.p once to create its new ledger, then resume_bootstrap.p.
An existing ledger is never overwritten implicitly; explicitly choose resume=1
or overwrite_checkpoint=1. Interrupted fits restart from recorded initial values,
while completed tasks are reused. This is not serialization of Ceres internals.

Accepted syntax:
  multistartfit(model,starts,x,y[,options])
  profilefit(model,theta0,x,y,one_based_coordinate,grid[,options])
  fitbootstrap(model,theta0,x,y[,options])
Models use curvefit's convention: unknown parameter arguments first and a trailing
real independent variable. Intermediate trailing fixed inputs use parameters.
The output is one explicitly chosen real scalar. Fitted inputs may be real or
Pentarnion scalars; complete unknown arguments contribute all five real coefficient
coordinates. No algebra coefficient is silently discarded or reassociated.
The initial adapter supports1..16 unknown real coordinates and2..4096 observations.
starts has one explicit candidate per row, preserving stable task IDs. A profile
fixes one flattened coordinate, removes it from the nuisance optimization vector,
and refits every remaining coordinate with Ceres. Equal-bound/frozen-width hacks
are not used. A one-coordinate model profiles by direct objective evaluation.

The optional Qt-free penta_inference DLL/SO schedules experiments. It reuses the
existing native penta_fit C ABI/Ceres driver and compiler AD model Jacobians.
Each task owns its Ceres workspace/native input buffers. Shared outer CPU workers
use inner_threads=1; native callbacks/operation quotas are reentrant and atomic.
Only the compiler-generated model is evaluated inside native fit loops, not AST
interpretation. Unchanged bodies reuse their compiled callbacks across runs,
parameters, profiles and resamples. Tiny jobs may use fewer actual workers.

objective="sse" (default) minimizes SSE/2 and returns descriptive profile delta_sse.
objective="gaussian_known_variance" requires an explicit positive noise_variance,
representing IID homoskedastic Gaussian observation errors. Costs then equal
SSE/(2*noise_variance), and profile deviance is (SSE_profile-SSE_local_best)/variance.
No variance is silently estimated and called known. Baselines/nuisance fits are
local optimizations: a negative deviance can reveal an improved baseline and is
flagged baseline_improved. profile_valid masks usable profile objective values;
invalid rows use finite zero sentinels and must not be interpreted as valid costs.
No automatic Wilks confidence threshold, global optimum or parameter-uniqueness
claim is made. Observation noise assumptions remain the user's scientific choice.

Bootstrap sample_method:
  pairs: IID matched x/y rows, sampled together with replacement;
  residual: centered residuals sampled IID, added to the fitted mean at fixed x;
  moving_block: centered circular residual blocks with an explicit block_length.
Residual sampling assumes a suitable common residual distribution; moving blocks
require an appropriate ordered/stationary residual dependence approximation and
a justified block length. They do not automatically model arbitrary correlations,
heteroskedasticity, censoring or experimental replicates. Task-specific RNG streams
are deterministic for seed/task identity, independent of worker scheduling.

prediction_x defaults to the observed x grid and may select bounded new locations.
Returned fitted_curves contain the refitted model mean, one task per row.
fitted_lower/upper are pointwise percentile bands across successful full-rank
refits at confidence (default.95), not simultaneous bands or intervals for future
noisy observations. parameter_std/correlation are empirical descriptive refit
quantities under the stated resampling assumptions. correlation_defined masks
zero-spread coordinates. Baseline/task ranks and rank_deficient are reported.
Rank-deficient convergence is numerical convergence without unique parameters;
those refits are excluded from parameter/band summaries. statistics_valid is zero
when fewer than two full-rank successful refits remain. A flat deliberately
unidentifiable example is included; fit convergence alone proves no uniqueness.
Coverage studies across many independently generated datasets remain pending.

Task status:0 pending,1 converged,2 not converged,3 failed,4 converged with a
rank-deficient nuisance Jacobian. task_status, ranks, messages, evaluations and
iterations retain task-local failures rather than discarding the whole batch.
Only valid rows should be interpreted scientifically. Numerical array/message
budgets apply; reported text can be truncated while complete messages stay in the
checkpoint. Bounds/parameter_scale and LM/dogleg reuse the existing driver.
MATLAB-style OutputFcn/OutputEvery are supported for every iterative native
Ceres refit. Declare the monitor as (matrix x,record optimValues=iterationinfo(),
string state="iter") to expose the typed information schema; it
returns0 continue or1 clean stop. x always contains the complete physical
parameter vector, including the fixed profile coordinate. optimValues.start_index
is0 for the prerequisite baseline fit and a one-based stable catalogue task ID
otherwise. INIT/ITER/DONE report actual solver iterations, never task-completion
surrogates. Parallel workers enqueue bounded immutable snapshots and wait for the
initiating thread to observe them. Observation order between independent tasks
can vary. Native arithmetic/plotting in the observer stays inside the owner's
reserved CPU slot; no interpreted objective/model body runs on pool workers.
A nonmutating monitor adds no objective/model evaluation. Returning1 gives
status="output_function_stop", user_stopped=1 and preserves unfinished rows as
pending; cancellation and convergence remain distinct. Observer exceptions release
all workers before propagating. Reentrant fitting/optimization/root solves from
a monitor remain forbidden. Checkpoint resume restarts interrupted tasks from
recorded initial points; observers are observations and are not checkpoint identity.
Robust losses and retained per-refit history arrays remain unsupported here.

Common options: parameters,method (lm/dogleg),lower,upper,parameter_scale,
max_iterations,max_evaluations,ftol,gtol,xtol,threads,seed,fit_wall_seconds,
max_wall_seconds (whole experiment),max_native_operations,prediction_x,batch_size.
fitbootstrap adds samples (default128),sample_method,block_length,confidence.
Observation options: OutputFcn,OutputEvery. See live-parallel-refit-models.p.
Checkpoint options: checkpoint,resume,overwrite_checkpoint,retry_failed.
Source/model/algebra bindings, solver settings, starts/data/seeds, both module
binaries and scientific outputs are versioned/hashed. Worker count/batch schedule
can change on resume. Corrupt/incompatible ledgers are rejected without replacement.
Atomic completed-task ledgers are data only; no source is executed when loading.
Hashes check integrity, not authentication. A failed checkpoint is reported without
discarding numerical results. Checkpointing is bounded to64MiB and2,000,000 scalar
result coordinates; other retained-array budgets also apply. Selected task fits
can be inspected via parameters/fitted_curves and replayed with curvefit/model.

Independent checks cover analytic linear regression/profile curvature, explicit
known-variance objectives, no-nuisance profiles, deliberately unidentifiable flat
profiles, deterministic IID/residual/block refits, actual shared-pool workers,
model failure isolation, checkpoint interruption/resume and corruption/settings
rejection. This is native scalar-curve inference; ODE/PDE likelihood adapters,
heterogeneous noise, general predictive uncertainty and full identifiability theory
are separate later stages, not implied by the initial workflow.
