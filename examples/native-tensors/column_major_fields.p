% Genuine rank-three fields use column-major entries and full algebra values.
clc;
clear;
algebra H = builtin_algebra("quaternion");
tensor<2,2,2> temperature = tensor([1,3,5,7;2,4,6,8],[2,2,2]);
display("Field shape and packed coefficient rows:",size(temperature),tensorvalues(temperature));
display("Entry (2,1,2), equivalently linear entry6:",temperature(2,1,2));
tensor<H,2,2,2> orientation = tensor(H.i,[2,2,2]);
orientation(2,1,2) = H.j;
tensor<H,2,2,2> right_action = orientation*H.i;
tensor<H,2,2,2> left_action = H.i*orientation;
display("Ordered right/left products at (2,1,2):",right_action(2,1,2),left_action(2,1,2));
display("Retained rank, immutable algebra hash and coefficients:",tensorinfo(orientation),tensorvalues(orientation));
