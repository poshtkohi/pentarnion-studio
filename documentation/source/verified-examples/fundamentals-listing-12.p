pentarnion p = 1+i+j+ell;
pentarnion q = 1-i+j-ell;
figure(1,"Name","Addition and product","NumberTitle","off");
subplot(1,2,1);
plot2("i","j",p,q,p+q,p*q);
title("Projection onto $(i,j)$");
subplot(1,2,2);
plot3("i","j","k",p,q,p+q,p*q);
title("Projection onto $(i,j,k)$");
