PARALLEL MATRIX OPERATIONS

Open project.pprj in Studio and Run main.p, or use penta-run main.p.
The script uses a fixed random seed, compares one/four-thread native products,
prints wall times and actual host workers, and restores automatic threads.
It warms the module/persistent workers before timing. Timings vary; this is a
warm end-to-end script illustration, not a promised speedup. A cold, very short
product can finish on the driver before newly created workers have awakened.

Expected assertions/results:
- Serial and parallel products agree within1e-10 Frobenius norm.
- A per-call threads override leaves threads()==4 unchanged.
- A row times a column gives the1x1 matrix [32].
- Batch products return A and2*A; column sums [5,7,9], row means [2;5].
- The two explicitly parenthesized Pentarnion triple products differ.

matmul(A,B,options(threads=n)) and batchmul(tupleA,tupleB,options(...)) have
per-call settings. The ordinary operator* uses the session threads(n) default.
Batches are tuples of1..16 matrices, not N-dimensional arrays. Uniform shapes
and element widths use one packed native batch and can distribute independent
batch tiles across workers. Heterogeneous or large-input batches use bounded
native pairs while retaining tuple order and per-pair types. Products have
bounded native work/allocation and cooperative tile cancellation.
Real products use OpenBLAS CBLAS tiles with one BLAS thread inside each host
worker. Pentarnion products use ordered binary scalar products and increasing
inner-index accumulation; matrix chains are never silently reassociated.
Entrywise operations and independent transforms/reduction lines reuse the
shared process CPU budget. Reductions preserve their documented axis/order.
