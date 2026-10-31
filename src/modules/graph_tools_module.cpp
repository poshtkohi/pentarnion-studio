#include "graph_tools_api.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>
namespace {
struct Failure:std::runtime_error{int status;Failure(int code,const char* text):std::runtime_error(text),status(code){}};
void message(char* out,uint64_t capacity,const char* text){if(out&&capacity){const auto size=std::min<uint64_t>(capacity-1,std::strlen(text));std::memcpy(out,text,size);out[size]=0;}}
struct Work {
    const penta_graph_host_v1* host;uint64_t maximum,count=0;
    void add(uint64_t amount=1){if(host->cancelled&&host->cancelled(host->context))throw Failure(PENTA_NUMERIC_CANCELLED,"Graph operation cancelled.");if(amount>maximum-std::min(count,maximum))throw Failure(PENTA_GRAPH_WORK_LIMIT,"Graph sparse/generator work budget exceeded.");count+=amount;if(host->charge){char error[512]{};if(host->charge(host->context,amount,error,sizeof(error)))throw Failure(PENTA_GRAPH_WORK_LIMIT,error[0]?error:"Graph host quota reached.");}}
};
void graphInput(const penta_graph_tools_request_v1& r,Work& work){
    if(r.nodes>100000||r.edges>500000||r.directed>1||r.multigraph>1||(r.edges&&(!r.endpoints||!r.weights)))throw Failure(PENTA_NUMERIC_ARGUMENT,"Graph endpoint/weight input is invalid.");
    std::set<std::pair<uint64_t,uint64_t>> edges;
    for(uint64_t e=0;e<r.edges;++e){work.add();const auto a=r.endpoints[2*e],b=r.endpoints[2*e+1];if(a<1||a>r.nodes||b<1||b>r.nodes||!std::isfinite(r.weights[e]))throw Failure(PENTA_NUMERIC_ARGUMENT,"Graph endpoints and real weights must be finite and in range.");const auto key=r.directed?std::pair{a,b}:std::pair{std::min(a,b),std::max(a,b)};if(!r.multigraph&&!edges.insert(key).second)throw Failure(PENTA_NUMERIC_ARGUMENT,"Parallel edges require multigraph=1.");}
}
void sparse(const penta_graph_tools_request_v1& r,penta_graph_tools_result_v1& result,Work& work){
    graphInput(r,work);if(r.kind<PENTA_GRAPH_SPARSE_ADJACENCY||r.kind>PENTA_GRAPH_SPARSE_LAPLACIAN||r.weighted>1)throw Failure(PENTA_NUMERIC_ARGUMENT,"Unknown graph sparse operator.");
    const uint64_t columns=r.kind==PENTA_GRAPH_SPARSE_INCIDENCE?r.edges:r.nodes;if(r.nodes>100000||columns>99999||result.pointer_capacity<columns+1||!result.column_pointers)throw Failure(PENTA_NUMERIC_ARGUMENT,"Graph CSC dimensions exceed canonical sparse storage/capacity.");
    std::map<std::pair<uint64_t,uint64_t>,double> entries;
    auto add=[&](uint64_t row,uint64_t column,double value){work.add();auto& entry=entries[{column,row}];entry+=value;if(!std::isfinite(entry))throw Failure(PENTA_NUMERIC_DOMAIN,"Graph sparse coefficients overflowed.");};
    for(uint64_t e=0;e<r.edges;++e){const auto a=r.endpoints[2*e]-1,b=r.endpoints[2*e+1]-1;const double w=r.weights[e];
        if(r.kind==PENTA_GRAPH_SPARSE_INCIDENCE){const double factor=r.weighted?w:1;add(a,e,-factor);add(b,e,factor);}
        else if(r.kind==PENTA_GRAPH_SPARSE_ADJACENCY){add(a,b,w);if(!r.directed&&a!=b)add(b,a,w);}
        else{add(a,a,w);add(a,b,-w);if(!r.directed&&a!=b){add(b,b,w);add(b,a,-w);}}
    }
    uint64_t count=0;for(const auto& [key,value]:entries){(void)key;if(value!=0)++count;}
    if(count>result.entry_capacity||(count&&(!result.row_indices||!result.values)))throw Failure(PENTA_NUMERIC_ARGUMENT,"Graph CSC output entry capacity is insufficient.");
    uint64_t index=0,column=0;result.column_pointers[0]=0;for(const auto& [key,value]:entries)if(value!=0){work.add();while(column<key.first)result.column_pointers[++column]=index;result.row_indices[index]=key.second;result.values[index++]=value;}
    while(column<columns)result.column_pointers[++column]=index;
    result.rows=r.nodes;result.columns=columns;result.entries=index;result.edges=r.edges;
}
uint64_t uniform(std::mt19937_64& rng,uint64_t bound){const auto threshold=uint64_t(-bound)%bound;for(;;){const auto value=rng();if(value>=threshold)return value%bound;}}
void generate(const penta_graph_tools_request_v1& r,penta_graph_tools_result_v1& result,Work& work){
    if(r.nodes>100000||r.directed>1||!std::isfinite(r.generated_weight)||r.seed>4294967295ull)throw Failure(PENTA_NUMERIC_ARGUMENT,"Generated graph size, weight, direction or seed is invalid.");
    std::vector<std::pair<uint64_t,uint64_t>> edges;
    auto edge=[&](uint64_t a,uint64_t b){work.add();if(edges.size()>=500000)throw Failure(PENTA_NUMERIC_ARGUMENT,"Generated graph exceeds500000 physical edges.");edges.emplace_back(a+1,b+1);};
    if(r.kind==PENTA_GRAPH_GENERATE_PATH){for(uint64_t n=1;n<r.nodes;++n)edge(n-1,n);}
    else if(r.kind==PENTA_GRAPH_GENERATE_CYCLE){if(r.nodes<(r.directed?2u:3u))throw Failure(PENTA_NUMERIC_ARGUMENT,"A simple cycle requires at least three undirected or two directed nodes.");for(uint64_t n=0;n<r.nodes;++n)edge(n,(n+1)%r.nodes);}
    else if(r.kind==PENTA_GRAPH_GENERATE_GRID){if(!r.grid_rows||!r.grid_columns||r.grid_rows>100000/r.grid_columns||r.grid_rows*r.grid_columns!=r.nodes)throw Failure(PENTA_NUMERIC_ARGUMENT,"Grid rows times columns must equal the requested node count.");for(uint64_t y=0;y<r.grid_rows;++y)for(uint64_t x=0;x<r.grid_columns;++x){const auto n=y*r.grid_columns+x;if(x+1<r.grid_columns)edge(n,n+1);if(y+1<r.grid_rows)edge(n,n+r.grid_columns);}}
    else if(r.kind==PENTA_GRAPH_GENERATE_GNM){const auto universe=r.nodes*(r.nodes?r.nodes-1:0)/(r.directed?1:2);if(r.requested_edges>std::min<uint64_t>(500000,universe))throw Failure(PENTA_NUMERIC_ARGUMENT,"G(n,m) edge count exceeds the simple loop-free edge universe.");std::mt19937_64 rng(r.seed);std::unordered_set<uint64_t> sampled;sampled.reserve(static_cast<std::size_t>(r.requested_edges*2));for(uint64_t j=universe-r.requested_edges;j<universe;++j){work.add();const auto candidate=uniform(rng,j+1);sampled.insert(sampled.contains(candidate)?j:candidate);}std::vector<uint64_t> ids(sampled.begin(),sampled.end());std::sort(ids.begin(),ids.end());for(auto id:ids){if(r.directed){const auto a=id/(r.nodes-1),offset=id%(r.nodes-1);edge(a,offset>=a?offset+1:offset);}else{uint64_t low=0,high=r.nodes-1;auto start=[&](uint64_t row){return row*(2*r.nodes-row-1)/2;};while(low+1<high){const auto middle=(low+high)/2;if(start(middle)<=id)low=middle;else high=middle;}edge(low,low+1+id-start(low));}}}
    else throw Failure(PENTA_NUMERIC_ARGUMENT,"Generator must be path, cycle, grid or gnm.");
    if(edges.size()>result.endpoint_capacity/2||(!edges.empty()&&!result.endpoints))throw Failure(PENTA_NUMERIC_ARGUMENT,"Generated edge output capacity is insufficient.");
    for(std::size_t e=0;e<edges.size();++e){result.endpoints[2*e]=edges[e].first;result.endpoints[2*e+1]=edges[e].second;}result.rows=edges.size();result.columns=2;result.edges=edges.size();
}
int execute(const penta_graph_host_v1* host,const penta_graph_tools_request_v1* request,penta_graph_tools_result_v1* result,char* error,uint64_t capacity)noexcept{
    try{if(!host||host->struct_size<sizeof(*host)||!request||request->struct_size<sizeof(*request)||!result||result->struct_size<sizeof(*result))throw Failure(PENTA_NUMERIC_ARGUMENT,"Graph tools ABI structures are incomplete.");Work work{host,request->max_work?request->max_work:1000000000};work.add();if(request->operation==PENTA_GRAPH_TOOLS_SPARSE)sparse(*request,*result,work);else if(request->operation==PENTA_GRAPH_TOOLS_GENERATE)generate(*request,*result,work);else throw Failure(PENTA_NUMERIC_ARGUMENT,"Unknown graph tools operation.");result->work_units=work.count;return PENTA_NUMERIC_OK;}catch(const Failure& e){message(error,capacity,e.what());return e.status;}catch(const std::exception& e){message(error,capacity,e.what());return PENTA_NUMERIC_INTERNAL;}catch(...){message(error,capacity,"Native graph tools failed.");return PENTA_NUMERIC_INTERNAL;}
}
const penta_graph_tools_api_v1 api{sizeof(api),PENTA_GRAPH_TOOLS_ABI_V1,"penta_graph_tools",execute};
}
extern "C" PENTA_NUMERIC_EXPORT const penta_graph_tools_api_v1* penta_graph_tools_query_v1(uint32_t version){return version==PENTA_GRAPH_TOOLS_ABI_V1?&api:nullptr;}
