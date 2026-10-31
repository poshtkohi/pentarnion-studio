% Native triangle meshes retain real coordinates, faces and boundary tags.
clc; clear; close("all");
record sphere_mesh = meshsphere(1, options(refinements=3));
record cylinder_mesh = meshcylinder(0.7, 2, options(segments=48, layers=8, caps=1));
record disk_boundary = meshcircle(1, options(segments=96));
record sphere_info=meshinfo(sphere_mesh);
record cylinder_info=meshinfo(cylinder_mesh);
display("Sphere area / enclosed volume: ", sphere_info.area, sphere_info.signed_volume);
display("Cylinder area / enclosed volume: ", cylinder_info.area, cylinder_info.signed_volume);
display("Polygon approximation to circle length: ", meshinfo(disk_boundary).length);

record hit = raysphere([-2,0,0], [1,0,0], [0,0,0], 1);
display("Ray-sphere intersection points: ", hit.points);
record plane_hit = lineplane([0,0,2], [0,0,-1], [0,0,0], [0,0,1]);
display("Line-plane intersection: ", plane_hit.points);

figure(1, "Name", "Native real geometry", "NumberTitle", "off");
subplot(1,2,1); meshplot(sphere_mesh); title("Triangulated sphere"); axis("equal");
subplot(1,2,2); meshplot(cylinder_mesh); title("Tagged cylinder with caps"); axis("equal");
% Data-only export does not overwrite files. Use a fresh path if needed:
% meshwrite(sphere_mesh, "sphere-surface.pmesh");
% record loaded = meshread("sphere-surface.pmesh");
