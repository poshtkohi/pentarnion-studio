NATIVE HYBRID EVENT SENSITIVITIES AND ADJOINTS

main.p: falling body, one localized impact and native restitution reset. It shows
event-time derivatives, the saltation matrix and fixed-time sensitivities. Each
continuous plotting epoch is drawn separately. hybridval selects exact pre/post
states and Hermite-interpolates inside an epoch without bridging a reset.

moving-terminal-event.p: stops after the reset and contrasts derivatives at the
moving event time with fixed-time post-reset derivatives. They must not be
interchanged in objectives or fitted observations.

fixed-time-hybrid-adjoint.p: native terminal objective with TRUE checkpointed
CVODES reverse integration on each smooth epoch. Covectors cross resets through
Xi^T; parameter gradients include Bp^T*lambda. It is not a renamed forward dot
product. Native running/event costs and moving-terminal boundaries are illustrated
in running-event-and-moving-terminal-costs.p. The event cost is once per localized
group at its original pre-reset state. Implicit DAE consistency/projection
derivatives remain separate.

pentarnion-section-reset.p: a complete five-coordinate Pentarnion field, scalar
section, conjugation reset and native terminal objective. All five coefficients
and parameter directions remain present, with analytic references printed.

Callback signature order is(time,declared states...,original parameters...).
Bodies are native and cached. Parameter/initial/tolerance edits reuse them.
simultaneous-sections-and-priorities.p: a native fixed-vector event, indexed
reset and scalar priorities. It contrasts one winning reset with two composed
resets and prints independent exact final sensitivities. Equivalent scaled
sections share a differentiable crossing time; different competing event-time
gradients reject sensitivities. Explicit sensitivities="none" permits a
deterministic value-only branch and reports unavailable derivative operators.
Per-component direction/terminal flags and up to16 roots are supported.
Initial-on-surface ambiguity and event-count endpoint derivative boundaries
are rejected; grazing derivatives are rejected.
max_events/minimum_event_interval guard chattering/Zeno behavior. max_bytes is a
documented native buffer forecast, not a process-RSS guarantee. Keep max_steps
bounded for larger-state adjoint checkpoint forecasts.

These are explicit OdeModel workflows. Existing generic IDAS daesolve is separate;
ODE saltation formulas are not silently applied to its consistency projection.

stiff-nonlinear-arkode-sensitivities.p exercises native ERK/ESDIRK forward and
reverse paths. ESDIRK's augmented implicit Jacobian uses generated second
derivatives. Reverse integration retains native polynomials at every accepted
step; coarse/fine output sampling leaves those checkpoints/derivatives unchanged.
Storage exhaustion rejects rather than falling back to coarse samples.

Windows isolated native83 checks passed: analytic bounce and all event-time/jump
terms, independent finite differences, true adjoint/forward references, all five
Pentarnion coefficients, safe jump queries, cache reuse, budgets and cancellation.
See specs/hybrid_saltation_v0_2.txt. Integrated/runtime/platform acceptance belongs
in HANDOFF and must follow executed checks.
