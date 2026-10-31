NATIVE MODEL AND ODE FITTING

Open project.pprj in Studio and Run each script separately.

main.p: native curvefit with Ceres LM, Dogleg and independent Huber residual
losses. Expected fitted line intercept1.04,slope1.98; changed data gives2,3 using
the same cached model library; robust location is1.25 despite the outlier100.
The four subplots show observations, fitted model, residuals and cost history.
Covariance is an explicitly requested local linear/Gaussian approximation,
estimated from weighted residual variance. It does not prove uniqueness.

ode_fit.p: fit a decay rate and initial state simultaneously. Nonuniform
observation times are exact native CVODES outputs. Compiler-generated J_x and
J_theta drive forward trajectory sensitivities supplied to Ceres. Expected
rate0.7 and initial state2.0. The script shows fitted trajectory and residuals.

pentarnion_fit.p: all five coordinates of one Pentarnion unknown are fitted
against the ordered native residual a*p-target. Coefficient weights are explicit;
the projected expected and fitted vectors overlap. No associative reordering.

Bodies compile to cached DLL/SO code. Data, parameter values, initial guesses,
weights, solver selection and bounds are runtime values. Native-only callbacks
have the documented scalar/Pentarnion subset; no interpreted solve fallback.
Budgets, cancellation, native/backend evaluation counts and histories are
reported. Covariance is unavailable for robust losses, active bounds or rank
deficiency. General optimizer gradients do not differentiate an opaque ODE;
odefit uses the explicit CVODES sensitivity adapter instead.
