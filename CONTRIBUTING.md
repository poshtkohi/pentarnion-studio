# Contributing

Pentarnion Studio combines a typed scientific language, native numerical modules
and a Qt desktop. Start with `README.md`, `language.txt` and the relevant contract
under `specs/`. Dependency versions, download hashes and integration patches live
under `third-party/` and `tools/`; bootstrap downloads are explicit operations.

Discuss a substantial language or mathematical change before implementing it.
Keep changes focused and describe the problem, resulting behavior and mathematical
assumptions. A language extension should update its specification, Help and a small
meaningfully named example. Document bounds and unsupported cases explicitly.

Preserve operand order and parentheses for nonassociative algebras. Distinguish
reciprocals from left/right equation solves, scalar from elementwise operations,
and compile-time model structure from runtime parameters. Avoid silently changing
the meaning of existing scripts or replacing requested native callbacks with
interpreted ones. Coordinate parallel work through the shared CPU thread budget.

Use a separate build directory. Run tests appropriate to the affected component,
including independent numerical reference cases when mathematical behavior changes.
Check finite values, domains, shapes, cancellation and failure diagnostics. Use
bounded worker counts and offscreen GUI fixtures for routine interface checks;
report platform, compiler, dependency versions, settings and skipped checks.
Passing a small example is not proof of general solver accuracy or stability.

Do not commit generated SDKs, caches, build trees, credentials, private documents or
personal datasets. Examples and test data must be synthetic, redistributable and
documented. Preserve third-party authorship and licence notices. Contributions are
reviewed under the repository licence and applicable third-party terms; do not add
code or data you lack permission to contribute.
