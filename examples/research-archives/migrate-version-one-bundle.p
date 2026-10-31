record study=options(value=3.0,status="completed");
saveexperiment("legacy-study.pexp.json",study,options(name="Legacy study",overwrite=1));
migrateexperiment("legacy-study.pexp.json","migrated-study.archive.json",options(overwrite=1));
record info=archiveinfo("migrated-study.archive.json");
display(info);
assert(info.history_complete==0);
record loaded=loadarchive("migrated-study.archive.json",study);
assert(loaded.value==3);
% A format migration preserves the old document; unavailable history is not invented.
