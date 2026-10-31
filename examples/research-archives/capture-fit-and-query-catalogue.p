native function real bowl(real x){return (x-2)^2;}
record fit=minimize(bowl,0.0);
record study=options(format="fit",status=fit.status,cost=fit.objective,parameters=fit.x);
savearchive("native-fit.archive.json",study,
    options(name="Native quadratic fit",settings=options(unit_system="dimensionless"),overwrite=1));
record info=archiveinfo("native-fit.archive.json");
display("Scientific identity: ",info.scientific_hash);
display("Automatically recorded calls: ",info.calls);
display("Accepted history complete: ",info.history_complete);
record catalogue=experimentcatalog(".",options(family="fit",scalar_path="cost",maximum=0.001));
display("Matching studies: ",catalogue.count);
if(catalogue.count>0){record first=catalogueentry(catalogue,1);display(first);}
record loaded=loadarchive("native-fit.archive.json",study);
assert(loaded.parameters==study.parameters);
