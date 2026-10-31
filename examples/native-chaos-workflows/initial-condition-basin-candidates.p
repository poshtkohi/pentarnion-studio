% Two stable equilibria of x' = x-x^3. Candidate IDs follow first occurrence.
ode bistable(t, real x) { x' = x-x*x*x; }
matrix initials = [-2; -0.5; 0.2; 1; 2; 4];
record b = basins(bistable, initials, options(t1=16, sample_step=0.05,
    escape_radius=3, threads=4, cluster_tolerance=0.01));
display("Candidate labels (0 unresolved, -1 escape, -2 failure, -3 pending): ", b.labels);
display("Fractions of all six initial conditions: ", b.fractions);
display("Conditional fractions among resolved candidates: ", b.conditional_fractions);
figure(1, "Name", "Finite initial-condition basin candidates");
plot(initials, b.labels);
xlabel("Initial x"); ylabel("Finite-tail candidate label");
caption("Sample fractions are not global basin measures or proofs of attractors.");
