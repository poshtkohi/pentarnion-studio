LYAPUNOV SPECTRA, STABILITY AND POINCARE SECTIONS

Open project.pprj and run main.p. It validates the exact flow spectrum [-1,-2],
five copies of -0.5 for one Pentarnion contraction state, the discrete-map exponent
log(0.5), stable-equilibrium
classification and an event-localized oscillator return interval of 2*pi.
The figure shows running estimates and the actual trajectory/section samples.

lorenz.p independently checks the sum of finite-time exponents against the
constant divergence -41/3. Individual exponents require observation/transient/
QR-interval/tolerance convergence studies and are not asserted as exact values.

paper1_lyapunov.p reuses the first-paper bracketing family and coefficient values.
It is an analysis example, not a new proof that the published trajectory is
chaotic or bounded. Changing A,B,eta or solver settings reuses the same native
ODE body. Explicit multiplication grouping remains part of its cache identity.

Current scope: CVODES BDF/Adams and native discrete-map tangent dynamics,
full finite-time QR spectrum,
local growth histories, residual-checked flow/map stability, localized event
sections and bounded recurrence analysis. Periodic-orbit shooting, Floquet
multipliers, true branch continuation and automatic attractor/basin classification
remain separate unimplemented subcases; they are not substituted by an orbit plot.
