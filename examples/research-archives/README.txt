Research archive examples (extension17)

capture-fit-and-query-catalogue.p records a native fit with automatic call metadata,
loads its typed data, and filters verified experiments by family and scalar cost.

replay-captured-random-and-native-model.p captures source/RNG history and explicitly
replays it in an isolated fresh session/output directory. Loading/cataloguing is
data-only. Current-prefix capture excludes the save/replay suffix of this script.

migrate-version-one-bundle.p wraps verified v1 data without inventing unavailable
source history or changing any numerical/algebra context.

Optional embed_artifacts=options(label="relative/file") captures verified opaque
bytes. archiveartifact(path,label) returns those bytes and never installs/loads
native code. DLL/SO binaries retain platform tags; they do not become portable
executables by being saved. See specs/research_archives_v0_2.txt for bounded capture
omissions, context guards, explicit replay side effects and external data files.
