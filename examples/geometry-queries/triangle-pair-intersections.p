% Physical surface intersections, including coplanar overlap polygons.
record first=trimesh([0,0,0;2,0,0;0,2,0],[1,2,3]);
record transverse=trimesh([.5,-1,-1;.5,2,-1;.5,.5,1],[1,2,3]);
record contact=meshintersect(first,transverse,options(first_values=[0;2;2],threads=0));
assert(contact.primitive_count==1);
assert(contact.dimensions(1)==1);
assert(norm(contact.first_values-contact.points(:,1)-contact.points(:,2),"fro")<1e-9);
display("Crossing segment endpoints: ",contact.points);
display("Original triangle pairs: ",contact.first_faces,contact.second_faces);
figure(1,"Name","Triangle-pair intersections","NumberTitle","off");
subplot(1,2,1);plot3(contact.points(:,1),contact.points(:,2),contact.points(:,3));
title("Transverse segment");xlabel("x");ylabel("y");zlabel("z");
record second=trimesh([.25,.25,0;1,.25,0;.25,1,0],[1,2,3]);
record overlap=meshintersect(first,second);
assert(overlap.coplanar_pairs==1&&overlap.dimensions(1)==2);
matrix closed=[overlap.points;overlap.points(1,:)];
subplot(1,2,2);plot3(closed(:,1),closed(:,2),closed(:,3));
title("Coplanar overlap polygon");
display("Overlap polygon: ",overlap.points);

% Complete immutable algebra fields interpolate on both contact surfaces.
algebra H=builtin_algebra("quaternion");
matrix<H,3,1> field=[1+H.i;2+H.j;3+H.k];
record sampled=meshintersect(first,second,options(first_values=field));
assert(size(sampled.first_values,2)==4);
assert(sampled.first_algebra_hash==algebra_info(H).hash);
display("Four retained quaternion coefficient columns: ",sampled.first_values);
