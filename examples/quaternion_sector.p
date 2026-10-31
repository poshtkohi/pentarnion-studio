use_algebra("A_gamma", gamma=0);
% At gamma=0 only the span of 1,i,j,k is the quaternion subalgebra.
pentarnion p = 1+i;
pentarnion q = j-k;
pentarnion r = 2-i+j;
pentarnion a1 = (p*q)*r;
pentarnion a2 = p*(q*r);
% The checker does not prove a1=a2 or compute their values.
