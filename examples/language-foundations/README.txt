REUSABLE SCIENTIFIC LANGUAGE
Open project.pprj in Studio, select main.p, and Run. Relative imports resolve
beside main.p; penta-run main.p also runs this example.

Expected deterministic lines (the final timing varies):
  bounds: 8 12
  scaled: 13
  changed gain: 4
  captured callback: 10
  module callback: 9
  complex unit: complex(0, 1)
  complex square: complex(-1, 0)
  Pentarnion: 1 + i + j
  native cube: 27
  execution seconds (compilation excluded): <nonnegative seconds>
Signed zero formatting of the complex square may depend on the platform.

SYNTAX AND SEMANTICS
- callback f=@named_function; or @(real x)=>expression. Named builtin handles
  use their scalar signature; anonymous wrappers select other overloads.
- Anonymous captures are snapshots made when the callback is constructed.
  Clearing or changing the original variable does not alter the captured data.
- function [real a,real b] f(real x){return x-1,x+1;} supports output lists.
  [real lo,real hi]=f(2); captures both; real lo=f(2); uses the first output.
- options(...) builds immutable typed fields. with_options(defaults,key=value)
  rejects unknown fields/types. Use a constant options default on a function
  parameter to declare its accepted field schema. Named arguments may reorder
  declared parameters. Positional arguments must precede named ones.
- import "file.p" as namespace; checks source without executing it in Check.
  Run executes the module in its own scope. Module functions retain access to
  module functions; module numerical variables are accessed through namespace.
- Complex values use complex(re,im), realpart, imagpart, arg and real embeddings.
  They cannot be mixed implicitly with Pentarnions; i retains its algebra basis.
- native function currently supports pure Real/Pentarnion parameters/results,
  local variables, assignment, if/else and return, scalar algebra/math. Arrays,
  callbacks, records, IO, loops and helper calls receive explicit diagnostics.
- save/load supports complex and pure-data records/tuples/logical arrays;
  callbacks and records containing them remain source objects and are rejected.

Imports have a 16-level depth limit and 8 MiB aggregate source budget. Nested
values/captures count toward workspace resource limits. Language 0.1 retains
its previous meaning of identifiers that become reserved names in 0.2.
