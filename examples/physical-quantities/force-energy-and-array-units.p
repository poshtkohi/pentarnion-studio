% Explicit elementwise quantity arithmetic for real scalar/array magnitudes.
clc;clear;
record masses=quantity([1,2,3],"kg");
record acceleration=quantity(9.81,"m/s^2");
record forces=unitmul(masses,acceleration);
display("Forces in newtons: ",unitvalues(unitconvert(forces,"N")));
record displacement=quantity([10,20,30],"cm");
record work=unitmul(forces,displacement);
display("Explicit aligned force-times-displacement values in joules: ",unitvalues(unitconvert(work,"J")));
record extra=quantity([50,100,150],"g");
record total_mass=unitadd(masses,extra);
display("Masses after unit-compatible addition: ",unitvalues(total_mass));
record square_length=unitpow(quantity(2,"cm"),2);
display("Squared length in square centimetres: ",unitvalues(unitconvert(square_length,"cm^2")));
% SI quantities are optional records; ordinary numeric/AD semantics are unchanged.
% This first quantity API is real-only. A Pentarnion coefficient or explicitly
% defined real physical diagnostic must be chosen before assigning SI units.
