Global sensitivity: initial native model stage of second roadmap Task6A

Run morris_screening.p, sobol_interactions.p or pentarnion_input_screening.p.
Accepted syntax: sensitivity(model,lower,upper[,options]). Bounds correspond to
1..32 flattened real input coordinates. One Pentarnion contributes all five
coefficients in (1,i,j,k,ell) order. The variable coordinates must complete the
leading declared real/Pentarnion arguments; optional parameters binds trailing
fixed arguments. A model returns one explicitly chosen real scalar summary.
The body is compiled and cached in the existing native compiler; changing bounds,
fixed runtime parameters, seed or sampling settings reuses an unchanged body.
The optional penta_sensitivity DLL/SO contains the numerical experiment loops.
No interpreter/AST evaluation happens inside native sampling workers.

Distribution assumption: all variable coordinates are independent uniforms on
finite strictly increasing intervals. Correlated inputs, alternative distributions,
conditional/group indices and dependent-data observation noise are not supported.
These are global input sensitivity estimates, not parameter-identifiability or
confidence claims about a fitted scientific model.

method="morris": samples is the number of randomized grid trajectories. levels
must be even in4..100 (default6). Each trajectory uses dimensions+1 evaluations.
mu is the signed mean, mu_star the mean absolute elementary effect, sigma the
sample dispersion. Effects divide by the normalized grid step Delta=p/[2(p-1)],
so they measure changes across an input's unit interval, including its physical
range width. Dispersion can reflect nonlinearity/interactions, not statistical
noise certainty. No Morris bootstrap interval is claimed in this version.

method="sobol": samples is the number N of independent paired Monte Carlo rows,
not the length of a Sobol low-discrepancy sequence. Native code evaluates A, B and
A with column i replaced by B_i (N*(dimensions+2) calls). The first_order estimator
is mean((Y_B-pooled_mean)*(Y_ABi-Y_A))/pooled_variance (Saltelli); total_order is
mean((Y_A-Y_ABi)^2)/(2*pooled_variance) (Jansen). The pooled population variance
uses A/B only. Raw finite-sample indices may be negative or greater than one;
they are not clipped or silently normalized. Constant sampled output variance
is rejected because variance fractions are undefined.

Optional bootstrap=B reuses the matched A/B/hybrid rows, resampling rows together
with replacement; no extra model evaluations. first_lower/upper and total_lower/
upper are percentile intervals at confidence (default.95), conditional on these
independent Monte Carlo sampling assumptions. They describe finite-sample index
uncertainty, not observational noise, fitted-parameter uncertainty, simultaneous
coverage or a general statistical guarantee. Zero-variance bootstrap replicates
are rejected clearly. bootstrap=0 returns uncertainty="not_estimated" and omits
interval fields. Bootstrap row generation and sample task seeds are deterministic
for a fixed seed and task identity, independent of scheduling.

Options: method, samples (default1024 Sobol/64 Morris), seed (nonnegative integer),
levels, threads, bootstrap (0..2000), confidence, parameters, max_evaluations
(default1,000,000; hard8,000,000), max_native_operations and max_wall_seconds.
Sampling rows are bounded to200,000; workspace to8,000,000 scalar evaluations;
bootstrap work B*N*d to200,000,000. Worker count0 inherits auto policy; explicit
positive limits and nested native calls use Studio's single bounded CPU pool.
Reports distinguish requested/granted/actually-used workers; tiny jobs can use
one participant. Native callbacks reserve a shared atomic per-analysis work
quota and never mutate interpreter state on workers. Cancellation and errors join
all workers before returning; seeds/scientific arrays remain reproducible.

Tests compare independent linear/product/Ishigami analytic integrals, normalized
Morris effects, exact serial/parallel estimates and bootstrap resampling, constant
offset robustness, bounds/zero variance/cancellation and nested CPU-budget reuse.
ODE/map/PDE native summary adapters, Morris/Sobol support for richer distributions,
profile likelihood with nuisance refits, observational/block bootstrap, prediction
bands and identifiability diagnostics remain future Task6 stages. The initial
compiler-compatible scalar model is not advertised as those larger workflows.
