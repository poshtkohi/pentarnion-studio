% Explicit grouping must be preserved for this nonassociative algebra.
% This file is editable source; no values are calculated in S0/S1.

pentarnion p = i;
pentarnion q = j;
pentarnion left = (p*q)*q;
pentarnion right = p*(q*q);

% Canonical reciprocal multiplication and equation division are distinct.
pentarnion reciprocal_product = p*inv(q);
pentarnion equation_quotient = p/q;
