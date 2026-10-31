native function real shifted_mean(matrix<1,8> values,real offset){
    matrix<1,1> total=sum(values);return total(1)/8+offset;
}
vector observed=[1,1.2,1.1,1.4,1.3,1.5,1.6,1.4];
record fitted=bootstrap(observed,options(statistic=shifted_mean,
    parameters=options(offset=0),method="moving_block",block_length=3,
    replicates=512,confidence=0.95,seed=42,threads=4));
display("Observed statistic: ",fitted.original);
display("Block percentile interval: ",fitted.lower,fitted.upper);
display("Native statistic cache key: ",fitted.cache_key);
record shifted=bootstrap(observed,options(statistic=shifted_mean,
    parameters=options(offset=1),method="moving_block",block_length=3,
    replicates=512,confidence=0.95,seed=42,threads=2));
assert(shifted.cache_key==fitted.cache_key);
display("Changed fixed parameter, same compiled body: ",shifted.original);
figure(1,"Name","Block bootstrap distribution","NumberTitle","off");
histplot(fitted.estimates,1:0.025:1.8);xlabel("Resampled statistic");ylabel("Count");
% The block method assumes ordered stationary dependence. It does not turn
% dependent observations into IID data or certify nominal coverage automatically.
