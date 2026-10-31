# Development roadmap

Current syntax, mathematical semantics and explicit bounds are described by
`language.txt` and `specs/`; runnable examples illustrate those contracts. A broad
module name does not imply that every possible method or mathematical subcase is
implemented. The following are continuing development directions rather than a
claim that unfinished extensions already work.

- Strengthen independent numerical references, conservation/convergence checks,
  failure diagnostics and regression coverage across supported platforms.
- Measure rendering responsiveness, native compilation/cache behavior and parallel
  numerical scaling with reproducible bounded benchmarks and accuracy comparisons.
- Improve portable dependency restoration, CI and contributor build instructions.
- Complete regenerated public manuals, searchable worked examples and documented
  interchange/export compatibility, retaining editable equations and source data.
- Extend algebra, dynamics, geometry, PDE and inference capabilities through
  explicit reviewed contracts, meaningful examples and validation before release.

Proposals should explain the scientific use case, API, dependency/licence impact,
threading/cancellation behavior and a realistic validation plan. Research claims,
software capability and numerical evidence should remain clearly distinguished.
