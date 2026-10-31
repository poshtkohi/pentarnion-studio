use_algebra("A_gamma",gamma=1);
clc;
clear;

% Existing sym() still treats atoms as formal nonassociative symbols.
% symexpand() explicitly applies the published ordered multiplication table.
record basis_product=symexpand("i*j",options(gamma="1"));
display("Exact k coefficient, sqrt(2)/2:",symcoeff(basis_product,3));
display("Exact ell coefficient, sqrt(2)/2:",symcoeff(basis_product,4));
assert(norm2(sympentnumeric(basis_product)-i*j)<1e-28);

record left=symexpand("(i*j)*ell",options(gamma="1"));
record right=symexpand("i*(j*ell)",options(gamma="1"));
record symbolic_associator=symexpand("(i*j)*ell-i*(j*ell)",options(gamma="1"));
display("Original grouped source:",left.source_expression,right.source_expression);
display("Left grouping coefficients:",sympenttext(left));
display("Right grouping coefficients:",sympenttext(right));
display("Their nonzero exact associator:",sympenttext(symbolic_associator));
assert(norm2(sympentnumeric(symbolic_associator))>0.01);

record exact_radical=symexpand("sqrt(8)-2*sqrt(2)");
assert(norm2(sympentnumeric(exact_radical))==0);
display("Exact radical cancellation:",sympenttext(exact_radical));

% A symbolic record may have its own explicit gamma. Conversion to a runtime
% Pentarnion requires the same immutable workspace context.
record half_table=symexpand("i*j",options(gamma="1/2"));
display("Exact gamma=1/2 coefficients (kept symbolic here):",sympenttext(half_table));
