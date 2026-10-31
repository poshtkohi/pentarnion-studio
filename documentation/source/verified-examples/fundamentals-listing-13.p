matrix measurements = [0,1;1,2;2,4];
writematrix(measurements,"measurements.csv");
writematrix(measurements,"measurements.xlsx","Results");
matrix restored = readmatrix("measurements.xlsx","Results");
assert(restored == measurements);
