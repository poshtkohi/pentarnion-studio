DISCRETE DYNAMICS AND PENTARNION MAPS

Open project.pprj in Studio and run main.p. It creates four subplots with the
logistic orbit, its successive-value return map, a sampled parameter orbit
diagram and a three-coordinate projection of an affine Pentarnion map. All
callback values and Jacobians execute compiled native DLL/SO code. Parameters,
initial states, iteration counts, transients and plot changes reuse the callback
structure; changing its algebraic body changes the cache key.

Expected counts: logistic orbit201 retained states (n200..400), return map200
pairs, parameter diagram1845 samples (45 independent parameters, n180..220),
Pentarnion affine map181 states and a5-by-5 real-coordinate final Jacobian.
The initial logistic update at r3.7, x0=.2 is x1=.592. The Jacobian is
r*(1-2*x) at the returned final state. The affine Pentarnion Jacobian is the
real left-multiplication operator of a; no associative identities are introduced.
An independent loop using the existing interpreted algebra gives the affine map
endpoint approximately (0.0630691772,-0.0577008841,-0.1368749405,-0.0365273127,
0.4682647499). The example checks each native endpoint coefficient to1e-10.

mapiterate defaults: iterations100, transient0, stride1, jacobian"analytic".
The retained sample indices are transient, transient+stride,..., plus the final
iteration when off-stride. The initial state is indexed0. Transients are actually
iterated. The requested iteration count includes the transient. Map records
retain flattened real states, an orbit array, n, the final x and native report.

mapdiagram starts from the same supplied initial state for every parameter
value. Only declared trailing real runtime parameters may be scanned. No branch
continuation, stability classification, Lyapunov exponent or chaos certification
is implied. The figure joins sampled points for display; dense points are not
proof of a bifurcation. Map Explorer's no-code GUI remains roadmap item20.

returnmap pairs samples separated by a positive lag in the retained sequence;
component is one-based in the real-coordinate states. It reports the source and
target iteration indices, so a retained off-stride final sample stays explicit.
It also accepts real/Pentarnion vectors or real-coordinate sample matrices.

For data-only CLI execution, omit figure/plot statements or use a separate
script with the map commands. penta-run deliberately disables figure rendering.
The runtime acceptance test executes this complete example and checks all four
plots and retained samples. Native Windows/Linux shared-library paths are
implemented; platform execution evidence belongs in the release handoff.
