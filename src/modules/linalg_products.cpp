#include "linalg_internal.hpp"
#include <cblas.h>
namespace penta::linalg {
namespace {
struct Product {const penta_numeric_host_v1* host;const penta_linalg_request_v1* request;penta_linalg_result_v1* result;};
int productTask(void* raw,uint64_t first,uint64_t last,char* error,uint64_t capacity){return protect([&]{auto& task=*static_cast<Product*>(raw);const auto& r=*task.request;auto& output=task.result->output[0];const auto& a=r.a;const auto& b=r.b;
    // Scope each actual host worker, rather than a manager waiting for it.
    // OpenMP caller ICVs are not inherited by unrelated std::thread workers;
    // no parent lease is held while waiting behind a configuration writer.
    std::optional<BlasScope> workerScope;if(output.width==1&&first<last)workerScope.emplace(1,task.host);
    constexpr uint64_t block=32;const auto tileRows=(a.rows+block-1)/block,tileCols=(b.columns+block-1)/block,perBatch=tileRows*tileCols;
    const algebra::Context context(r.gamma);
    for(auto tile=first;tile<last;++tile){cancelled(task.host);const auto batch=tile/perBatch,local=tile%perBatch,row=(local/tileCols)*block,col=(local%tileCols)*block;
        const auto rows=std::min(block,a.rows-row),cols=std::min(block,b.columns-col);
        if(output.width==1){cblas_dgemm(CblasRowMajor,CblasNoTrans,CblasNoTrans,static_cast<int>(rows),static_cast<int>(cols),static_cast<int>(a.columns),1.0,a.values+batch*a.rows*a.columns+row*a.columns,static_cast<int>(a.columns),b.values+batch*b.rows*b.columns+col,static_cast<int>(b.columns),0.0,output.values+batch*a.rows*b.columns+row*b.columns+col,static_cast<int>(b.columns));
            for(uint64_t i=0;i<rows;++i)for(uint64_t j=0;j<cols;++j)finite(output.values[batch*a.rows*b.columns+(row+i)*b.columns+col+j]);
        }else for(uint64_t i=row;i<row+rows;++i)for(uint64_t j=col;j<col+cols;++j){auto sum=algebra::Pentarnion::scalar(context,0);
            for(uint64_t k=0;k<a.columns;++k){if((k&255u)==0)cancelled(task.host);sum=algebra::add(sum,algebra::multiply(element(a,batch*a.rows*a.columns+i*a.columns+k,context),element(b,batch*b.rows*b.columns+k*b.columns+j,context)));}
            write(output,batch*a.rows*b.columns+i*b.columns+j,sum);
        }
    }
},error,capacity);}
int transformTask(void* raw,uint64_t first,uint64_t last,char* error,uint64_t capacity){return protect([&]{auto& task=*static_cast<Product*>(raw);const auto& r=*task.request;auto& output=task.result->output[0];const auto& a=r.a;const algebra::Context context(r.gamma);
    for(auto index=first;index<last;++index){if((index&255u)==0)cancelled(task.host);
        if(r.operation==PENTA_LA_TRANSPOSE){auto value=element(a,index,context);if(r.conjugate)value=algebra::conjugate(value);const auto row=index/a.columns,col=index%a.columns;write(output,col*a.rows+row,value);}
        else {const auto row=index/a.columns,col=index%a.columns;const auto value=element(a,index,context);const bool right=r.operation==PENTA_LA_RIGHT_OPERATOR;const auto op=right?algebra::rightMatrix(value):algebra::leftMatrix(value);
            for(uint64_t i=0;i<5;++i)for(uint64_t j=0;j<5;++j){const auto outRow=right?col*5+i:row*5+i,outCol=right?row*5+j:col*5+j;output.values[outRow*output.columns+outCol]=finite(op[i][j]);}
        }
    }
},error,capacity);}
int reductionTask(void* raw,uint64_t first,uint64_t last,char* error,uint64_t capacity){return protect([&]{auto& task=*static_cast<Product*>(raw);const auto& r=*task.request;const auto& a=r.a;auto& output=task.result->output[0];const auto length=r.axis==1?a.rows:a.columns;const algebra::Context context(r.gamma);
    for(auto entryIndex=first;entryIndex<last;++entryIndex){cancelled(task.host);algebra::Coefficients result{};
        for(uint32_t component=0;component<a.width;++component){long double total=0;double extreme=0;for(uint64_t k=0;k<length;++k){if((k&255u)==0)cancelled(task.host);const auto index=r.axis==1?k*a.columns+entryIndex:entryIndex*a.columns+k;const auto x=a.values[index*a.width+component];
            if(r.reduce_operation==PENTA_LA_MIN)extreme=k?std::min(extreme,x):x;else if(r.reduce_operation==PENTA_LA_MAX)extreme=k?std::max(extreme,x):x;else total+=x;
        }result[component]=r.reduce_operation==PENTA_LA_MIN||r.reduce_operation==PENTA_LA_MAX?extreme:finite(static_cast<double>(r.reduce_operation==PENTA_LA_MEAN?total/length:total));}
        write(output,entryIndex,algebra::Pentarnion(context,result));
    }
},error,capacity);}
}
void denseProduct(const penta_numeric_host_v1* host,const penta_linalg_request_v1& r,penta_linalg_result_v1& result){
    const auto batches=r.operation==PENTA_LA_MULTIPLY?std::max<uint64_t>(1,r.batches):1;matrixValid(r.a,batches);auto& output=result.output[0];Product task{host,&r,&result};char error[1024]{};int status=0;
    if(r.operation==PENTA_LA_MULTIPLY){matrixValid(r.b,batches);require(r.a.columns==r.b.rows,"Matrix multiplication requires columns(A)==rows(B).");require(r.a.rows*r.b.columns<=1000000/batches,"Product output exceeds native bound.");require(static_cast<long double>(r.a.rows)*r.a.columns*r.b.columns*batches*(std::max(r.a.width,r.b.width)==5?25:2)<=1000000000.0L,"Product exceeds its native arithmetic-work bound.");outputValid(output,r.a.rows,r.b.columns,std::max(r.a.width,r.b.width),batches);std::fill_n(output.values,r.a.rows*r.b.columns*batches*output.width,0.0);
        if(r.a.columns==0||r.a.rows==0||r.b.columns==0){char emptyError[128]{};const auto emptyStatus=host->parallel_for(host->context,0,1,productTask,&task,emptyError,sizeof(emptyError));if(emptyStatus)throw Error(emptyStatus,emptyError);result.converged=1;return;}
        const auto tiles=((r.a.rows+31)/32)*((r.b.columns+31)/32)*batches;
        result.backend_threads=1;
        const auto cost=std::max<uint64_t>(1,32*32*r.a.columns*(output.width==5?25:2));
        status=host->parallel_for(host->context,tiles,cost,productTask,&task,error,sizeof(error));
    }else if(r.operation==PENTA_LA_REDUCE){require(r.axis==1||r.axis==2,"Reduction axis must be1 or2.");require(r.reduce_operation>=PENTA_LA_SUM&&r.reduce_operation<=PENTA_LA_MAX,"Unknown matrix reduction.");require(r.a.width==1||r.reduce_operation==PENTA_LA_SUM||r.reduce_operation==PENTA_LA_MEAN,"Pentarnion arrays have no min/max ordering.");const auto length=r.axis==1?r.a.rows:r.a.columns;require(length||r.reduce_operation==PENTA_LA_SUM,"Empty mean/min/max is undefined.");outputValid(output,r.axis==1?1:r.a.rows,r.axis==1?r.a.columns:1,r.a.width);status=host->parallel_for(host->context,output.rows*output.columns,std::max<uint64_t>(1,length*r.a.width),reductionTask,&task,error,sizeof(error));
    }else {require(r.operation==PENTA_LA_TRANSPOSE||r.operation==PENTA_LA_LEFT_OPERATOR||r.operation==PENTA_LA_RIGHT_OPERATOR,"Unknown linear transform.");if(r.operation==PENTA_LA_TRANSPOSE)outputValid(output,r.a.columns,r.a.rows,r.a.width);else outputValid(output,(r.operation==PENTA_LA_RIGHT_OPERATOR?r.a.columns:r.a.rows)*5,(r.operation==PENTA_LA_RIGHT_OPERATOR?r.a.rows:r.a.columns)*5);status=host->parallel_for(host->context,r.a.count,r.operation==PENTA_LA_TRANSPOSE?r.a.width:25,transformTask,&task,error,sizeof(error));}
    if(status)throw Error(status,error[0]?error:"Native product/transform failed.");
    cancelled(host);result.converged=1;
}
}
