% A genuine two-dimensional surface embedded in all five coefficient axes.
% Its induced Euclidean metric is explicit; no unique R5 normal is invented.
use_algebra("A_gamma", gamma=1);
clc; clear; close("all");
native function pentarnion coefficient_surface(real u, real v, real amplitude) {
    return u + v*i + amplitude*sin(u)*cos(v)*j
             + amplitude*cos(u)*sin(v)*k + 0.25*u*v*ell;
}
record surface_mesh = paramesh(@coefficient_surface,
    linspace(-2,2,31), linspace(-2,2,31),
    options(parameters=options(amplitude=0.8), threads=0));
record geometry_info = meshinfo(surface_mesh);
display("Embedding dimension: ", geometry_info.embedding_dimension);
display("Induced area: ", geometry_info.area);
display("Enclosed three-dimensional volume available: ", geometry_info.volume_available);
display("All five coordinates remain in the mesh: ", size(surface_mesh.vertices));
figure(1, "Name", "Projections of one R5 surface", "NumberTitle", "off");
subplot(1,2,1); meshplot(surface_mesh, options(projection=[1,2,3]));
title("Projection onto $(1,i,j)$"); xlabel("$1$"); ylabel("$i$"); zlabel("$j$");
subplot(1,2,2); meshplot(surface_mesh, options(projection=[3,4,5]));
title("Projection onto $(j,k,\\ell)$"); xlabel("$j$"); ylabel("$k$"); zlabel("$\\ell$");
