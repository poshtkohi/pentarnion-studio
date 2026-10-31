use_algebra("A_gamma",gamma=1);
clc;
clear;
close("all");
record field=pentarray([1+i,1+j;1+k,1+ell],[2,2],
    options(axes=options(axis1="x",axis2="y"),
        coords=options(axis1=[0,1],axis2=[0,1])));
native function pentarnion square_element(pentarnion p){return p*p;}
record mapped=ndmap(field,@square_element,options(threads=0));
record multiplied=ndmul(field,field);
assert(norm(ndvalues(mapped)-ndvalues(multiplied),"fro")<1e-14);
display("All five native mapped coordinates:",ndvalues(mapped));

% Fourier complex values have a separate two-coordinate scalar domain. The
% algebra basis element i is never substituted for the Fourier imaginary unit.
record i_component=ndcoeff(field,1);
record fourier=ndfft(i_component,"x");
display("Complex FFT of the explicitly selected real i component:",ndvalues(fourier));
figure(1,"Name","Pentarnion field coefficient projections","NumberTitle","off");
subplot(1,2,1);ndplot(field,options(component=1));title("Source $i$ coefficient");
subplot(1,2,2);ndplot(mapped,options(component=0));title("Mapped scalar coefficient");
