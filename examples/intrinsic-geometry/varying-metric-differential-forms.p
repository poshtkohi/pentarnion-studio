native function matrix<2,2> polar_metric(real r,real theta) {return [1,0;0,r*r];}
native function matrix<1,2> radial_form(real r,real theta) {return [1,0];}
native function real potential(real r,real theta) {return r*r;}
record alpha=formfield(radial_form,[2,.4],1,options(metric=polar_metric));
record dual=hodge(alpha);
record delta=codifferential(alpha);
record f=formfield(potential,[2,.4],0,options(metric=polar_metric));
record laplace=formlaplacian(f);
display("Hodge dr=r dtheta, including metric derivative jets: ",dual.coefficients,dual.first);
display("Codifferential dr=-1/r, expected-0.5: ",delta.coefficients);
display("Hodge Laplacian r^2=-4: ",laplace.coefficients);
display("The scalar Hodge Laplacian has the opposite sign to div(grad f).");
