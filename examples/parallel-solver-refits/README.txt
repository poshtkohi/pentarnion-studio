Parallel native ODE and PDE refits

Open project.pprj and run either descriptively named script.
ode-multistart-profile-and-bootstrap.p demonstrates independent ODE parameter
fits, nuisance-profile infrastructure and residual-bootstrap fitted-mean bands.
pde-parameter-refits-at-an-observed-node.p fits a real FD reaction-diffusion model
at one selected node. The known uniform field gives an independent exponential
reference. Its Jacobians are explicitly finite differences of native PDE solves.

Both reuse the existing penta_inference DLL/Ceres scheduler and Studio's shared
CPU budget. ODE equations/reaction callbacks compile to cached native DLLs/SOs;
each fit owns native solver buffers. No interpreted scientific model runs inside
the fitting loop. Outer workers use inner_threads=1. Scalar observation lookups
reuse a cached complete trajectory/Jacobian at each physical parameter vector.

Pass complete fixed initials/parameters in options and select fit_parameters
(names/indices) plus optional fit_initial coordinates. theta0/starts contain the
selected physical fit coordinates. Select observation_coordinates explicitly for
subsets of an ODE state or PDE field. Native solver output times must actually
exist; no sample interpolation is substituted for PDE observations.

Use checkpoint/resume to retain completed independent fits. Interrupted fits
restart from their recorded initials; internal Ceres state is not serialized.
Numerical convergence/rank and bootstrap noise assumptions need interpretation.
The current bootstrap resamples scalar observations, not an inferred spatial or
multicomponent covariance model. Bands describe pointwise fitted means, not
future noisy observations or simultaneous coverage.

Full syntax, output layouts and limits: specs/solver_refits_v0_2.txt.
