# Preparing and publishing a public release

The copyright holders have confirmed Apache-2.0 and authority to release the
included first-party code, documentation and GTG-derived components. Public
source publication is authorised. Binary release verification and signing are
separate tasks; the licence grant does not claim either has been completed.

1. Retain the complete Apache-2.0 `LICENSE`, attribution `NOTICE` and original
   third-party notices. Apply this first-party licence consistently to included
   runtime/helpers without relicensing upstream dependencies or user models.
2. Review `THIRD_PARTY_NOTICES.md`, `docs/REDISTRIBUTION.md` and dependency locks.
   Preserve corresponding sources, patches, build configurations and any relink
   materials required by the actual binary dependency closure.
3. Run `python tools/audit-public-source.py --root . --release`. Review every
   finding; the audit is a useful check, not proof that no secret exists.
4. Build from a fresh checkout with the documented scripts, run the appropriate
   tests with bounded resources and record the exact commit/toolchain/dependency
   identities and results. Do not label untested platforms as verified.
5. Review the staged Git diff and file list. Start with a clean history; do not
   import development history, unpublished papers, account configuration, local
   caches, private datasets, compiler output or installed SDK trees.
6. Push the reviewed source without force-pushing or replacing existing remote
   history. The release workflow is manual; it is not a substitute for review.
7. Tag the reviewed source and create a draft GitHub Release. Upload the installer
   and binaries, matching source snapshot, checksums, notices and necessary
   corresponding-source/relink companions as release assets. Never commit these
   large binaries to Git. Inspect the draft before making it public.
8. Publish the release notes with actual validation and signing status. Do not
   claim SignPath approval, Windows certification, deterministic binary output or
   comprehensive platform validation unless those were separately established.

The historical Windows research-preview installer was built before the confirmed
Apache-2.0 release policy. Do not upload it unchanged as a binary of the newly
licensed source revision. Rebuild from the reviewed source, or transparently
repackage verified binaries with their exact matching editable implementation
and recorded original build identity. Include complete licence/notice and
corresponding-source/relink companions, update outdated private-distribution
wording, and verify generated-runtime notice companions. Record which binaries
were rebuilt or retained, and test the new package's exact hashes. Repackaging
does not establish that the current source checkout was freshly compiled.
Signing is not approved or claimed.

See `packaging/README.md` for the complete optional distribution implementation
and packaging prerequisites. Signing keys, service credentials and private SDKs
are never source or release assets.
