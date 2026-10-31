use_algebra("A_gamma",gamma=1);
clc;
clear;
close("all");
real pi_value=3.141592653589793;
vector time=0:16;
vector real_part=cos(2*pi_value*3*time/17);
vector imaginary_part=sin(2*pi_value*3*time/17);
record signal=complexarray(real_part,imaginary_part,[17],
    options(axes=options(axis1="time"),coords=options(axis1=time)));
record spectrum=ndfft(signal,"time");
record reconstructed=ndifft(spectrum,"time");
assert(norm(ndvalues(reconstructed)-ndvalues(signal),"fro")<1e-11);
display("Prime-length17 complex FFT reconstructed:",ndinfo(spectrum));
display("Interleaved real/imaginary coordinate rows:",ndvalues(spectrum));

record A=ndarray([1,2,3;4,5,6],[2,3]);
display("Column-major values, expected [1;4;2;5;3;6]:",ndvalues(A));
record transposed=ndpermute(A,[2,1]);
display("Permuted values, expected [1;2;3;4;5;6]:",ndvalues(transposed));
record reshaped=ndreshape(A,[3,2]);
display("Reshape preserves order and resets changed axis coordinates:",ndinfo(reshaped));
record changed=ndset(A,[2,1],9);
assert(ndvalues(ndget(changed,[2,1]))==[9]);
assert(ndvalues(ndget(A,[2,1]))==[4]);

record column=ndarray([1,2],[2,1]);
record row=ndarray([10,20,30],[1,3]);
record expanded=ndadd(column,row);
display("Matching-axis singleton expansion:",ndvalues(expanded));
figure(1,"Name","Arbitrary-length complex Fourier transform","NumberTitle","off");
subplot(1,2,1);plot(time,real_part);title("Real part of a complex oscillation");xlabel("Time index");
subplot(1,2,2);plot(time,ndvalues(ndabs(spectrum)));title("Magnitude of the length17 FFT");xlabel("Frequency bin");
