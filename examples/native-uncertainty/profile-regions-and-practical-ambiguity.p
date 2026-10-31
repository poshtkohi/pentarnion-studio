native function real square_model(real amplitude,real x){return amplitude*amplitude;}
record fits=multistartfit(square_model,[-2;2],[1,2,3],[4,4,4]);
record ambiguity=identifiability(fits);
display("Distinct near-optimal parameter clusters: ",ambiguity.clusters);
display("Representative original fit IDs: ",ambiguity.representative_run_ids);
display(ambiguity.notice);
native function real constant(real level,real x){return level;}
record profile=profilefit(constant,2,[1,2,3],[1,2,3],1,
    linspace(1,3,41),options(objective="gaussian_known_variance",noise_variance=0.04));
record regions=profileinterval(profile,options(confidence=0.95));
display("Sampled one-parameter likelihood regions: ",regions.intervals);
display("Open lower/upper boundaries: ",regions.lower_open,regions.upper_open);
figure(1,"Name","Sampled profile likelihood");plot(profile.grid,profile.deviance);
xlabel("level");ylabel("Likelihood deviance");
% A finite multistart/profile search does not prove structural identifiability.
