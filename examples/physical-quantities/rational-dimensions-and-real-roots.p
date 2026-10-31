clc;
record physical_length = unitpow(quantity(4,"cm^2"),1,2);
display("Square root of 4 square centimetres, metres: ",unitvalues(physical_length));
record signed_length = unitpow(quantity(-8,"m^3"),1,3);
display("Real cube root, metres: ",unitvalues(signed_length));
record fractional = quantity([1,2,3],"kg*m^(1/2)/s^(3/2)");
display("Exact SI numerators: ",fractional.numerators);
display("Exact SI denominators: ",fractional.denominators);
display("Compatible dimensions: ",unitcompatible(physical_length,"m"));
