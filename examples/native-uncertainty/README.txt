NATIVE INFERENCE, UNCERTAINTY AND DYNAMICAL SUMMARY EXTENSIONS

Each .p runs independently. Every numerical model, residual/event and derivative
is generated cached native code; the native Ceres/SUNDIALS modules do the solves.

Likelihood examples distinguish additive Gaussian variance, lognormal log-space
variance/model median, Student-t scale squared/explicit degrees of freedom and
Poisson count means. No statistical observation model is inferred from SSE.
Prediction examples distinguish parameter/fitted-mean bands from future noisy
observations, and pointwise bands from joint bands over a finite output grid.
Coverage studies generate independent datasets and refit every one. They report
both failed-interval-aware and conditional coverage with Monte Carlo uncertainty.

Practical ambiguity/profile diagnostics do not establish structural identifiability.
Period summaries use a selected scalar signal and three rising level crossings,
not proof of a fundamental orbit. Largest Lyapunov summaries require native
explicit CVODES variational flows; DAE/mass/reset tangent substitutes are rejected.
DAE final/temporal/period/event summaries use the native residual/IDAS path.
