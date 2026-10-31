native function real constant(real level,real x){return level;}
record study=coveragefit(constant,[2],1:8,options(initial=[1],samples=256,
    objective="gaussian_known_variance",noise_variance=0.04,
    confidence=0.95,seed=42,threads=4,checkpoint="coverage-study.json",
    overwrite_checkpoint=1));
display("Generating parameter: ",study.coverage.truth);
display("Empirical conditional coverage: ",study.coverage.conditional_coverage);
display("Unconditional coverage including failed intervals: ",study.coverage.unconditional_coverage);
display("Monte Carlo standard error: ",study.coverage.monte_carlo_standard_error);
display("Estimator bias/RMSE: ",study.coverage.bias,study.coverage.rmse);
display(study.coverage.notice);
% Each dataset has its own immutable RNG stream and a genuinely new native fit.
% For resume, use resume=1 with the same scientific settings and remove overwrite.
