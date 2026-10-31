function nd_reference(directory)
% Independent MATLAB oracle for artifacts emitted by penta_nd_tests DIR.
n=0:29;
field=reshape(cos(.2*n)+1i*sin(.3*n),[3,5,2]);
reference=fft(field,[],2);
actual=dlmread(fullfile(directory,'axis-fft.csv'),',');
assert(max(abs(actual(:,1)+1i*actual(:,2)-reference(:)))<1e-12);
A=reshape(1:6,[2,1,3]);B=reshape([10,20,30,40],[1,4,1]);
expanded=A+B;
assert(isequal(dlmread(fullfile(directory,'broadcast.csv')),expanded(:)));
original=reshape(1:24,[2,3,4]);permuted=permute(original,[3,1,2]);
assert(isequal(dlmread(fullfile(directory,'permutation.csv')),permuted(:)));
disp(['MATLAB_ND_REFERENCE_PASS ',version]);
end
