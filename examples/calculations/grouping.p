% These expressions differ because grouping matters.
use_algebra("A_gamma", gamma=1);
display("(i*j)*j =", (i*j)*j);
display("i*(j*j) =", i*(j*j));
display("reciprocal product =", (i*inv(j))*j);
pentarnion quotient = i/j;
display("equation residual norm2 =", norm2(quotient*j-i));
