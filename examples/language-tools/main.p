% Language tools: arrays, reusable functions, decisions and native data files.
use_algebra("A_gamma", gamma=1);
clc;
clear;
close("all");

function real energy(pentarnion p) {
    return norm2(p);
}
function pentarnion product(pentarnion p, pentarnion q) {
    return p*q;
}

pentarnion p = 1+i+j+ell;
pentarnion q = 1-i+j-ell;
pentarnion result = product(p,q);
assert(energy(result)>0,"The product should have positive squared norm.");
display("p*q =",result);

vector t = 0:0.025:6.25;
vector y = exp(-0.2*t).*sin(3*t);
matrix samples = [t.', y.'];
matrix tail = samples(end-9:end,:);
display("Last ten samples:",tail);
display("Mean and extrema:",mean(y),min(y),max(y));

integer total = 0;
for(n=1:8) {
    if(n==3) { continue; }
    total = total+n;
}
while(total<40) { total=total+1; }
if(total==40 && length(t)>100) {
    display("Loop and range checks passed.");
} else {
    error("Unexpected numerical result.");
}

rng(2026);
matrix random_values = rand(3,3);
display("Reproducible random values:",random_values);
writematrix(samples,"samples.csv");
writematrix(samples,"samples.xlsx","Trajectory");
matrix reloaded = readmatrix("samples.xlsx","Trajectory");
assert(reloaded==samples,"The XLSX round trip must preserve the sample values.");
save("experiment.pws","p","q","result","samples","total");
total=0;
load("experiment.pws","total");
assert(total==40);
who;
whos;
help("for");

figure(1,"Name","Language tools: damped oscillation","NumberTitle","off");
plot(t,y);
title("$y(t)=e^{-0.2t}\\sin(3t)$");
xlabel("$t$");ylabel("$y(t)$");
caption("Native array calculations, slicing and CSV/XLSX output");
