% Intrinsic S2(radius) x R2: the scalar curvature is2/radius^2.
native function matrix<4,4> product_metric(real theta,real phi,real z,real w,real radius) {
    return [radius*radius,0,0,0;0,radius*radius*sin(theta)^2,0,0;0,0,1,0;0,0,0,1];
}
record first=intrinsicgeometry(product_metric,[.8,.4,0,0],options(parameters=options(radius=2.0)));
record second=intrinsicgeometry(product_metric,[.8,.4,0,0],options(parameters=options(radius=3.0)));
display("Scalar curvature, expected0.5 and2/9: ",first.scalar_curvature,second.scalar_curvature);
display("Ricci tensor: ",first.ricci);
display("Torsion / metric compatibility: ",first.torsion_norm,first.metric_covariant_norm);
display("Changed radius reused the compiled metric: ",first.cache_keys==second.cache_keys,second.cache_hit);
