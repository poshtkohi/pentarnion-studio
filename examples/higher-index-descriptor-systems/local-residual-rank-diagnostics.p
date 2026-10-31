native function [real first,real second] residual(real t,real x,real z,real xp,real zp){
    return xp+x,z-x*x;
}
record local_info=daeanalyze(residual,0.0,[1,1],[-1,-2]);
display("Local index / rank contract: ",local_info.local_index,local_info.index_contract);
display("Generated Jy / Jyp: ",local_info.jacobian_y,local_info.jacobian_yp);
native function [real first,real second] higher(real t,real x,real z,real xp,real zp){
    return zp-x,z+sin(t);
}
record unsupported=daeanalyze(higher,0.0,[1,0],[0,1]);
display("A pointwise diagnostic does not invent a higher-index proof: ",unsupported.status);
