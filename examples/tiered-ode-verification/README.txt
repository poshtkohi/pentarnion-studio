TIERED ODE VERIFICATION

main.p screens two phases of a known normal-form cycle, then repeats selected
original run IDs over20/40 time units with tighter native solves. It attaches
phase-fixed periodic shooting/Floquet evidence and replays one exact tier.
Known references: period2*pi and transverse multiplier exp(-4*pi).

lorenz-finite-time-evidence.p calculates full-state recurrence and native tangent
QR spectra at40/80 time units. It independently repeats with the other CVODES
method, half the QR step and tighter tolerances. All differences/flags are shown.
The deliberately moderate agreement threshold is an example setting, not a
universal standard of scientific validation. Tighten and extend for research.
The word candidate is essential: finite observation does not certify chaos.

weak-and-neutral-growth.p demonstrates why a small positive exponent, or several
near-zero ones, does not establish chaos or quasiperiodicity. It uses analytic
growth and incommensurate linear oscillators with known spectra.

exclude-independent-work.p retains an auxiliary integral in the original full
solution, while excluding it from physical recurrence/Floquet diagnostics. Such
exclusion is allowed only for whole leading states that do not depend on removed
states. For two physical Pentarnion q/v states the dimension would be10.

Each example is a native numerical workflow. Model structure is compiled once;
parameter/initial/horizon/tolerance changes reuse the cached shared library.
No interpreter runs per integrator step/QR interval. threads uses Studio's shared
CPU pool. First-use compilation is excluded from tic/toc as elsewhere.

Evidence is data-only: saveanalysis/loadanalysis preserve it without executing
code. verifyrun checks original body compatibility and uses the recorded gamma
with raw real-coordinate values. Exact mid-tier checkpoint/resume, generalized
nonautonomous shooting and validated mathematical chaos proofs are future scope.

Windows isolated native45 checks passed, including exact diagonal exponents,
Lorenz divergence-sum, full-state recurrence, serial/parallel ordering, cache reuse,
auxiliary safety, conservative labels, nonautonomous status and cancellation.
Integrated CLI/GUI/platform acceptance is recorded separately in HANDOFF.
