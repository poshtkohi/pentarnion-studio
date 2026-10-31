record sphere=meshsphere(1,options(refinements=3));
record inside=meshcontains(sphere,[0,0,0;2,0,0;1,0,0]);
display("Oriented winding classification0=outside,1=inside,2=boundary: ",inside.classification);
record ray=meshray(sphere,[2,0,0;2,2,0],[-1,0,0]);
display("Ray first-hit flags / points / parameters: ",ray.classification,ray.points,ray.parameters);
figure(1,"Name","Closed solid triangle geometry","NumberTitle","off");
meshplot(sphere);title("Queries act on the retained triangles");
