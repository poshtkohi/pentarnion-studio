SAFE EXPERIMENT BUNDLES

Run snapshot-real-analysis.p or snapshot-imported-pentarnion-calculation.p.
Each intentionally overwrites only its named demonstration .pexp.json file via
options(overwrite=1), allowing the example to run again. The second also writes
selected-samples.csv. The default saveexperiment behavior refuses an existing
filename. Exports are bounded atomic JSON publications, not executable archives.

saveexperiment(path,typed_record[,options(name=...,settings=options(...),
artifacts=options(label="project-relative-file",...),overwrite=0)]) captures:
  - The full typed numerical/research record and optional scientific settings.
  - Exact current invocation bytes plus all recursive analyzed import snapshots,
    encoded as base64 with byte lengths and SHA-256 identities.
  - Current framework scientific provenance/native callback keys/backend identity.
  - Separately captured execution paths, compilation/scheduling metadata and time.
  - Optional explicitly selected external artifact filenames, byte sizes and hashes.

Artifact bytes remain external; keep referenced files separately. No automatic
DLL/cache packaging, installation, source execution or archive unpacking occurs.
Hashes detect inconsistency/corruption; they are not signatures/authentication.

loadexperiment(path,typed_template[,options(allow_real_context=0)]) validates
format/version, hashes, source/artifact manifests, context and the typed schema
before returning data. Existing loadanalysis strict context behavior is unchanged.
Only explicit allow_real_context=1 can import otherwise-compatible real-only
data across gamma. Typed Pentarnion scalars/arrays/solutions and context-bearing
domain records (including hidden N-D Pentarnion arrays) always require matching
context. Unsupported versions/language/precision/algebra require explicit future
migration; no silent migration or numerical-value substitution is performed.

experimentinfo(path) inspects compatibility and identities without executing or
decoding source. experimentsource(path[,"main"/import_id]) returns verified source
bytes as a string only. An artifact path must be portable/project-relative, with
no traversal, drive/device/ADS ambiguity or escape through symlinks/junctions.
On Windows the opened artifact's physical handle path is checked against the
opened project root; bytes come from that same read-only handle.

scientific_hash excludes captured execution metadata and only the recognized
framework provenance.execution subrecord in its payload projection. User data
named execution or elapsed_seconds remains scientific exactly as supplied. Select
stable scientific fields deliberately if passing complete solver records that
also contain timing data. Full integrity_hash still covers every retained byte.

The snapshot scope is the current invocation. A console save after an earlier
script does not include all previous workspace function bodies, values or command
history. workspace_history_included=0 makes that limit explicit. The main source
filename is unavailable in the core hook; its exact bytes/hash and source directory
are captured. Imports retain all original location metadata. A self-contained
replay environment is not claimed; replay requires deliberate review/action.

Ordinary export failures retain numerical workspace data and the prior committed
bundle. Cancellation inside saveexperiment serialization/publication is a narrow
R2053 runtime error: "Experiment export cancelled; completed numerical workspace
retained." It stops the script without the usual cancellation rollback. Numeric
solves, loading, context-changing transactions and cancellation outside this export
keep their established behavior. Cancellation before publication leaves the prior
file intact; a commit already completed is a valid atomic new bundle.
