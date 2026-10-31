# Code signing policy and readiness

**Current status:** the first-party project is licensed under Apache-2.0 with authority to release the included code, documentation and GTG-derived components confirmed. No SignPath application, certificate approval, signing request or signed release is claimed. The previous local installer is unsigned. Licence confirmation is not signing approval; this readiness document is not a badge asserting service sponsorship.

## Eligibility and trust

[SignPath Foundation's terms](https://signpath.org/terms) require an eligible OSI-licensed, maintained, documented and already released project, no disallowed proprietary components, verified source/build origin, MFA, review roles and manual release approval. Acceptance is discretionary. Its rules distinguish the project's own binaries from upstream binaries; unsigned upstream OSS files may be packaged, but a project certificate must not simply be applied to every vendor DLL/tool. Modified upstream signing has additional fork/review conditions.

Only after actual approval should the required SignPath attribution and assigned approver/reviewer identities be published as operative policy. Confirm the treatment of external MATLAB adapters and Microsoft's packaged platform runtimes with the service rather than claiming that every file is OSS.

An open-source signing utility does not supply a trusted public identity. [Microsoft's guidance](https://learn.microsoft.com/en-us/windows/apps/develop/smart-app-control/code-signing-for-smart-app-control) distinguishes a signature from a certificate trusted by Windows. [SmartScreen reputation guidance](https://learn.microsoft.com/windows/apps/package-and-deploy/smartscreen-reputation) also explains why new signed applications can still warn. Self-signed test certificates and checksum files are not substitutes for public trust. Do not instruct users to disable Windows protection or install an arbitrary root certificate as a public-distribution workaround.

## Release pipeline to prepare

- Retain the confirmed Apache-2.0 `LICENSE`, `NOTICE` and GTG provenance in the published source. Give maintainers, reviewers and signing approvers explicit responsibilities. Do not invent approval names or claim MFA settings have been enabled.
- Use a documented, verifiable build from a fixed commit with pinned dependencies and traceable patches. Keep build scripts and CI configuration in review scope. A local binary merely uploaded to a CI artifact store is not proof it was built from that source.
- Identify first-party artifacts separately from upstream Qt/GCC/Python and other binaries. The no-JBIG TIFF build and resource-patched compiler tools require transparent upstream provenance; do not present them as wholly authored by Pentarnion.
- Complete resource/manifest/branding changes first. If signing is approved, sign eligible first-party PE artifacts, refresh all affected SDK/file manifests and copies, then build and sign the final installer. Preserve the final signature/timestamp checks and SHA-256 values.
- Verify the exact signed package's startup, native callback compilation/loading, cache reuse, exports and safe update/uninstall. A signature changes bytes; earlier unsigned hashes and receipts do not certify the signed artifact.
- Never put a signing private key, certificate password or service token in the repository or distributed application. Use the approved service's protected workflow and human approval controls.

## Native compilation limitation

Studio compiles user equations/objectives to new local DLLs. Signing Studio or its installer does not sign each future generated model. Microsoft's documentation states that Smart App Control can evaluate executable files beyond downloaded installers. **Inference for this application:** strict SAC/WDAC policy may affect compiler tools or dynamically generated callback DLLs. Test the actual supported configuration; do not guarantee that a signed installer makes arbitrary generated native code admissible. Do not embed a public-trust signing key to sign arbitrary users' models.

## Network/privacy statement to verify for a release

Copilot is optional. In the inspected implementation, sending a question can transfer the user's prompt and selected context to their configured provider; the default welcome is local. Release documentation must accurately explain those providers and user controls. Signing policy must not promise that external providers never retain data or never charge for requests. No provider request or signing-service action was performed by this audit.

The detailed free-service conditions and Windows reputation policy can change. Recheck the linked official pages when applying and before the first signed release.
