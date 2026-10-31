clc;
clear;
record product = caspent("i*j",options(gamma="symbolic"));
record derivative = casdiff(product,"gamma");
display("Exact product for symbolic gamma:",product.expression);
display("Derivative with respect to gamma:",derivative.expression);
display("Derivative at gamma=0.4:",casnumeric(derivative,options(gamma=0.4)));

record bracket = caspent("(p*q)*r-p*(q*r)",
    options(pentarnions="p,q,r",gamma="symbolic"));
display("Associator scalar coefficient:",bracket.c0);
display("Associator ell coefficient:",bracket.c4);

% Native Pentarnion results belong to one immutable algebra context.
record fixed = cassubs(product,"gamma","1");
display("Specialized value:",caspentnumeric(fixed));
display("Checked source for a cached native shared module:",
    cascode(fixed,"first_table_product"));
