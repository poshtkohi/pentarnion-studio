#include "runtime/interpreter.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>
namespace {
int checks=0,failures=0;
void check(bool okay,const std::string& text){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<text<<'\n';}}
void print(const penta::runtime::ExecutionResult& result){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}
void valid(const std::string& source,const std::string& name){auto result=penta::runtime::execute(source);if(!result.ok())print(result);check(result.ok(),name);}
void invalid(const std::string& source,const std::string& name){check(!penta::runtime::execute(source).ok(),name);}
}
int main(){
    valid("matrix A=[4,1;1,3];matrix b=[1;2];matrix x=linsolve(A,b);assert(norm(A*x-b)<1e-12);assert(abs(det(A)-11)<1e-12);matrix inverse=inv(A);assert(norm(A*inverse-eye(2),\"fro\")<1e-12);record report=solve_report(A,b,options(method=\"cholesky\",threads=2));assert(report.converged);assert(report.backend_threads<=2);assert(report.relative_residual<1e-12);assert(report.rank==2);assert(report.rcond_available);", "real solve/inverse/determinant with diagnostics and per-call thread limit");
    valid("matrix A=[1,2;3,4;5,7];record L=lu(A);assert(norm(L.P*A-L.L*L.U,\"fro\")<1e-10);record Q=qr(A);assert(norm(A*Q.P-Q.Q*Q.R,\"fro\")<1e-10);record S=svd(A,options(threads=2));assert(norm(A-S.U*S.S*transpose(S.V),\"fro\")<1e-10);assert(S.rank==2);assert(rcond(A)>0);assert(cond(A)>1);matrix C=chol([4,1;1,3]);assert(norm(C*transpose(C)-[4,1;1,3],\"fro\")<1e-10);", "real LU/pivoted QR/thin SVD/Cholesky reconstructions");
    valid("matrix A=[0,-1;1,0];record e=eig(A);assert(abs(e.values_real(1))<1e-12);assert(abs(abs(e.values_imag(1))-1)<1e-12);matrix vr=e.vectors_real;matrix vi=e.vectors_imag;matrix wr=diag(e.values_real);matrix wi=diag(e.values_imag);assert(norm(A*vr-(vr*wr-vi*wi),\"fro\")<1e-10);assert(norm(A*vi-(vr*wi+vi*wr),\"fro\")<1e-10);record s=schur(A);assert(norm(A-s.Q*s.T*transpose(s.Q),\"fro\")<1e-10);", "split complex eigenvectors and real Schur data");
    valid("matrix A=[1,2;2,4];assert(rank(A)==1);assert(det(A)==0);record candidate=solve_report(A,[1;0],options(method=\"svd\"));assert(!candidate.converged);assert(candidate.residual_norm>0.1);assert(candidate.rank==1);", "least-squares candidate does not claim an equation solve");
    valid("threads(4);matrix warm=matmul(ones(128,128),eye(128),options(threads=4));clear warm;matrix A=ones(256,256);matrix B=eye(256);matrix C=matmul(A,B,options(threads=1));record serial=parallel_info();assert(serial.threads_used==1);assert(threads()==4);matrix D=matmul(A,B,options(threads=4));record workers=parallel_info();assert(workers.parallel_calls>serial.parallel_calls);assert(workers.threads_used>=1);assert(workers.threads_used<=4);assert(C==D);assert(D(1,1)==1);assert(D(end,end)==1);threads(0);", "warm256-square per-call dispatch maximum and session default preservation");
    valid("matrix A=[1,2;3,4];matrix E=eye(2);matrix F=2*E;tuple left=(A,A);tuple right=(E,F);record before=parallel_info();[matrix one,matrix two]=batchmul(left,right,options(threads=2));record after=parallel_info();assert(one==A);assert(two==2*A);assert(after.parallel_calls+after.serial_calls==before.parallel_calls+before.serial_calls+1);", "uniform tuple batches use one native packed dispatch");
    valid("matrix A=[1,2;3,4];matrix P=[1+i];[matrix real_product,pentarnionmatrix pent_product]=batchmul((A,P),(eye(2),[j]),options(threads=2));assert(real_product==A);assert(pent_product(1)==(1+i)*j);", "heterogeneous batch fallback preserves shapes and entry types");
    valid("matrix A=[1,2,3;4,5,6];matrix s=sum(A,1);matrix t=sum(A,2);matrix u=mean(A,2);matrix low=min(A,[],2);matrix high=max(A,[],1);assert(s==[5,7,9]);assert(t==[6;15]);assert(u==[2;5]);assert(low==[1;4]);assert(high==[4,5,6]);assert(transpose(A)==[1,4;2,5;3,6]);matrix M=sin(A);assert(abs(M(1,2)-sin(2))<1e-12);", "parallel matrix reductions, native transforms and entrywise functions");
    valid("logicalmatrix mask=transpose(logical([0,1]));matrix A=[10;20];matrix selected=A(mask);assert(selected(1)==20);logicalmatrix row=mask';matrix B=[30,40];matrix selected_row=B(row);assert(selected_row(1)==40);", "native transpose preserves logical-mask identity and selector semantics");
    valid("matrix A=[1,2,3;4,5,6];assert(min(A,3)==[1,2,3;3,3,3]);assert(max(A,3)==[3,3,3;4,5,6]);matrix E=zeros(0,3);matrix M=min(E);matrix X=max(E);assert(size(M,1)==0);assert(size(M,2)==3);assert(size(X,1)==0);assert(size(X,2)==3);", "pairwise scalar min/max and legacy empty reduction shapes are preserved");
    valid("matrix zero=sum([]);assert(numel(zero)==1);assert(zero(1)==0);", "legacy sum of0x0 literal remains a1x1 zero array");
    valid("matrix A=[2+0.1*i,0.05*j;0.03*ell,3+0.1*k];matrix X=[1+i+j,1-j+ell;1+ell,2+i];matrix B=A*X;matrix left=solveleft(A,B);for(c=0:4){assert(norm(coeff(left-X,c),\"fro\")<1e-9);}matrix C=X*A;matrix right=solveright(A,C);for(c=0:4){assert(norm(coeff(right-X,c),\"fro\")<1e-9);}matrix LA=leftop(A);matrix RA=rightop(A);assert(size(LA,1)==10);assert(size(RA,2)==10);assert(abs(det(LA))>0);matrix IL=inv(LA);assert(norm(LA*IL-eye(10),\"fro\")<1e-9);", "left/right Pentarnion equation operators and explicit real operator inverses");
    valid("matrix A=[1+0.2*i-0.3*j+0.4*k+0.5*ell];matrix B=[-0.1+0.4*i+0.2*j-0.5*k+0.1*ell];matrix C=[0.2-0.2*i+0.6*j+0.3*k-0.4*ell];matrix difference=(A*B)*C-A*(B*C);real magnitude=0;for(c=0:4){magnitude=magnitude+abs(coeff(difference(1),c));}assert(magnitude>1e-8);", "matrix product grouping preserves nonassociativity");
    valid("matrix A=[4,1;1,3];record S=sparse(A);assert(nnz(S)==4);assert(full(S)==A);matrix b=[1;2];matrix y=sparsemul(S,b);assert(y==A*b);record square=sparsemul(S,S);assert(full(square)==A*A);record T=sparsetranspose(S);assert(full(T)==transpose(A));matrix x=sparsesolve(S,b);assert(norm(A*x-b)<1e-12);record r=sparse_solve_report(S,b,options(method=\"bicgstab\",preconditioner=\"ilut\",threads=2,tolerance=1e-12));assert(r.converged);assert(!r.rcond_available);assert(norm(A*r.solution-b)<1e-10);", "canonical sparse storage/products/transpose and direct/iterative solves");
    valid("record S=sparse([1,1,2,2],[1,1,2,1],[2,3,7,0],2,2);assert(nnz(S)==2);assert(full(S)==[5,0;0,7]);record P=sparse([1,2],[1,2],[i,j],2,2);matrix B=[1;1];matrix C=sparsemul(P,B);assert(coeff(C(1),1)==1);assert(coeff(C(2),2)==1);record Q=sparsemul(P,P);matrix D=full(Q);assert(D==[i*i,0;0,j*j]);", "sparse duplicate assembly, zero omission and ordered Pentarnion sparse products");
    invalid("matrix a=inv([1,2;2,4]);", "singular inverse rejected");
    invalid("matrix a=chol([1,0;0,-1]);", "non-positive Cholesky rejected");
    invalid("matrix a=inv([i,j;j,i]);", "ambiguous whole-Pentarnion matrix inverse rejected");
    invalid("real d=det([i,j;j,i]);", "ambiguous whole-Pentarnion determinant rejected");
    invalid("matrix x=linsolve([1,2;2,4],[1;0]);", "singular LU solve rejected");
    invalid("matrix x=linsolve([1,2;2,4],[1;0],options(method=\"svd\"));", "unaccepted least-squares equation residual rejected by plain solve");
    invalid("matrix x=matmul(eye(2),eye(2),options(threads=65));", "invalid per-call thread setting rejected");
    invalid("matrix x=matmul(eye(2),eye(2),options(threds=4));", "unknown typed option rejected");
    invalid("record S=sparse([1],[3],[2],2,2);", "sparse triplet bounds enforced");
    invalid("record S=sparse(ones(2,2));record bad=with_options(S,row_indices=[1,1,1,2]);matrix A=full(bad);", "malformed sparse canonical record rejected at use");
    invalid("matrix x=sparsesolve(sparse(eye(2)),[1;2],options(rcond_min=0.9));", "sparse solve rejects unavailable reciprocal-condition safeguards rather than ignoring them");
    penta::runtime::ExecutionOptions budget;budget.maxOperations=100;auto bounded=penta::runtime::execute("matrix a=ones(32,32);matrix b=a*a;",{},budget);check(!bounded.ok(),"large native operations retain host operation budget");
    penta::runtime::ExecutionOptions storage;storage.maxMatrixElements=20;auto small=penta::runtime::execute("matrix A=eye(2);matrix op=leftop(A);",{},storage);check(!small.ok(),"coefficient operator allocation obeys dense array bound");
    std::cout<<checks<<" advanced linear algebra checks, "<<failures<<" failures\n";return failures?1:0;
}
