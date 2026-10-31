Independent native PDE experiments
==================================
Run paired-reaction-rate-scan.p, pentarnion-gamma-catalogue.p,
parallel-two-dimensional-fields.p, or plan-cartesian-pde-experiments.p.

pdeensemble(reaction,x,initial_rows,options(...)) handles 1D FD/P1 FE;
pdeensemble2(reaction,x,y,initial_rows,options(...)) handles structured 2D FD.
The 2D initial argument can be one ny-by-nx field, or rows of flattened
x-fastest fields. Real and Pentarnion states use the same solver semantics
as pdesolve/pdesolve2 (RK4 or explicitly named first-order IMEX Euler).

The reaction is compiled once to a cached native DLL/SO. Every independent
run has immutable parameter/gamma bindings. Outer work uses Studio's shared
CPU thread budget, inner PDE work receives one CPU slot. threads=0 inherits
automatic selection. Reports distinguish requested/granted/actual workers;
sparse Eigen factorisation/solves are not claimed to be parallel.

Default pairing="paired" broadcasts a single initial/parameter/gamma entry,
otherwise counts must match. Explicit pairing="cartesian" forms the full
initial x parameter x gamma product (gamma fastest). scan=options(rate=[...])
forms named finite parameter domains in sorted-name order; parameter_sets
accepts one record or a tuple of explicit records. dry_run=1 checks counts,
shapes and native compatibility without compilation/integration.

Summary outputs contain final_values, final_mean, final_rms, maximum_rms,
stop_times, source_indices and per-run status/errors. Failed runs are isolated;
check task_status before interpreting a result (0 pending,1 completed,2 failed).
Only selected retain=[one_based_ids] keep full fields (maximum16).
pderun(batch,id) accesses an already retained field; it never silently solves.
pdereplay(reaction,batch,id) explicitly verifies/replays saved settings/model.

All batch and retained values are plain REAL node-major coefficient columns:
value_layout="node_coefficients", components=1 or5, coefficient order1,i,j,k,ell.
This preserves different gamma contexts without falsely making a workspace-
labelled Pentarnion array. pdecoeff(field,c) extracts one real coefficient;
pdeslice(field,k,c) also requires an explicit coefficient for raw Pentarnions.
Animations read the saved gamma. pdeparams returns raw5-vectors for Pentarnion
parameters. pdesettings and pdereplay construct typed Pentarnion inputs only
when the selected gamma equals the workspace gamma; otherwise clear/set the
matching algebra and resume/recreate the raw batch first. Existing typed
pdesolve/pdesolve2 results and their accessors remain compatible.

checkpoint-completed-pde-runs.p writes an atomic versioned SHA256-checked
summary ledger; resume-completed-pde-runs.p reuses completed tasks and reruns
interrupted tasks from initial fields. This is NOT an exact mid-step restart.
Native source/backend/scientific inputs must match. Worker count, wall budgets
and batch scheduling can change. Summary-only checkpoints cannot be combined
with selected full-field retention on resume. Existing incompatible ledgers
are refused without modifying them. No native source/AST numerical loop is
interpreted on workers. Ordinary saveanalysis/loadanalysis still enforce their
existing workspace-algebra context guard.

Resource caps bound run count, retained coordinates, estimated workspace,
wall time and shared native work. The workspace estimate is not an RSS limit.
The batch API does not yet include adaptive PDE-aware discovery/SA adapters,
unstructured2D FE, nonlinear fully implicit reactions or distributed execution.
