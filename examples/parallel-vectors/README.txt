PARALLEL VECTOR OPERATIONS

Open project.pprj in Studio, open either source tab, then Run File/F5.
The scripts can also be run separately with penta-run from the Release bin.
No files are written by these scripts. Save before Run uses the normal Studio
workflow. The original first-paper project is independent of these examples.

real_vectors.p
  Requests four CPU threads, evaluates a large sine vector, computes ordered
  AXPY, real dot product and Euclidean norm, then demonstrates that a three-entry
  addition remains serial. parallel_info records requested/effective counts,
  workers actually participating, peak concurrent workers, dispatch counts,
  last element count, threshold and whether the optional module is loaded.
  The AXPY last value is approximately 2.8414709848079; the small vector is
  [5, 7, 9]. Timing and exact worker participation depend on the CPU/run and are
  deliberately not asserted as fixed benchmark results.

pentarnion_vectors.p
  Uses five-component entry kernels, shows p*q differs from q*p, and verifies
  the vector sine agrees with the published whole-element scalar sine.
  Expected first products are computed directly by the scalar algebra in the
  assertions; operands and parentheses are preserved by the vector backend.

threads(0) selects logical CPUs automatically, bounded to 64 workers. threads(n)
accepts an explicit integer 1..64. threads() returns the requested setting (0
means automatic). A session retains its setting until changed/reset; both
examples restore automatic selection before finishing. Small operations remain
serial. The process shares one persistent worker pool across numerical modules.

The penta_numerics DLL/SO is loaded only when a numerical module operation is
first used, and remains loaded until Studio exits. parallel_info does not force
loading it. Reopening a script in the same Studio process may therefore show a
module that was already loaded by earlier numerical work.

SUM/MEAN/DOT/NORM reductions use fixed 256-entry blocks and a fixed merge order
independent of CPU thread count. This gives repeatability across thread counts
within the same build/platform, without promising identical results across
different compilers, floating-point platforms or math libraries.

PER-CALL THREADING REVISION (categories5-8 implementation)
threads(n) sets the session default, rather than forcing every operation to use
the same worker count. A pure numeric call may pass a final options(threads=n),
for example sin(x,options(threads=1)), dot(x,y,options(threads=4)),
norm(x,options(threads=2)), sum(A,2,options(threads=1)) or
transpose(A,options(threads=2)). Options0 means automatic selection for that
call. Operators without options inherit the session default. Scope restoration
also applies when an operation errors or is cancelled. A maximum is not a claim
that every worker will participate; small/serial kernels still use fewer workers.

per_call_threads.p demonstrates one/four-session and one/two-call choices,
matching serial/parallel values, reductions, transforms and expm1. Expected
expm1(1) is approximately1.718281828459; thread participation varies by workload.
parallel_info requested/effective remain the session default; last_requested and
last_effective describe the last native numerical dispatch. threads_used and
peak_active describe actual participation. Per-call settings do not mutate the
session default or persist into later calls.

Native embedding code can pass a different ParallelSettings on each module call.
If a numerical worker invokes another numeric library, the inner call receives
one granted CPU slot and executes inline rather than multiplying worker pools.
Existing extra real array functions expm1/log1p/erf/erfc/sign now share the native
entrywise dispatch. Callback quadrature has separate options; derivative and
root options use their documented callback/solver records. Parallel matrix and
BLAS/LAPACK examples are in categories5/6.
