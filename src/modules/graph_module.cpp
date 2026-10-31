#define PENTA_NUMERIC_MODULE_BUILD
#include "graph_api.h"
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/connected_components.hpp>
#include <boost/graph/strong_components.hpp>
#include <boost/graph/topological_sort.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
struct Failure final : std::runtime_error {
    Failure(int statusCode,const char* text):std::runtime_error(text),status(statusCode){}
    int status;
};
void message(char* output,uint64_t capacity,const char* text) {
    if(output&&capacity){const auto count=std::min<uint64_t>(capacity-1,std::strlen(text));std::memcpy(output,text,count);output[count]=0;}
}
struct Budget {
    const penta_graph_host_v1& host;uint64_t limit,used=0,pending=0;
    void check() const {if(host.cancelled&&host.cancelled(host.context))throw Failure(PENTA_NUMERIC_CANCELLED,"Graph operation cancelled.");}
    void flush(){check();if(pending&&host.charge){char text[512]{};if(host.charge(host.context,pending,text,sizeof(text)))throw Failure(PENTA_GRAPH_WORK_LIMIT,text[0]?text:"Graph host work budget exceeded.");}pending=0;}
    void spend(uint64_t amount=1){if(amount>limit-used)throw Failure(PENTA_GRAPH_WORK_LIMIT,"Graph native work budget exceeded.");used+=amount;pending+=amount;if(pending>=256)flush();}
};
double realResult(long double value) {
    if(!std::isfinite(value)||std::abs(value)>std::numeric_limits<double>::max())throw Failure(PENTA_NUMERIC_DOMAIN,"Graph result exceeds the finite real range.");
    return static_cast<double>(value);
}
long double addDistance(long double a,long double b) {
    if(!std::isfinite(a))return a;
    const auto sum=a+b;if(!std::isfinite(sum))throw Failure(PENTA_NUMERIC_DOMAIN,"Graph path cost exceeds the finite accumulator range.");return sum;
}
struct Edge {double weight;uint64_t id;};
using Directed=boost::adjacency_list<boost::vecS,boost::vecS,boost::directedS,boost::no_property,Edge>;
using Undirected=boost::adjacency_list<boost::vecS,boost::vecS,boost::undirectedS>;
struct Input {
    const penta_graph_request_v1& request;
    std::size_t n,m;
    Directed graph;
    uint64_t selfLoops=0,negative=0;
    std::vector<uint64_t> in,out;
    std::vector<long double> strength;
    long double totalWeight=0;
    Input(const penta_graph_request_v1& r,Budget& budget):request(r),n(0),m(0),graph(0) {
        if(r.nodes>100000||r.edges>500000||r.directed>1||r.multigraph>1||(r.edges&&!r.endpoints))throw Failure(PENTA_NUMERIC_ARGUMENT,"Graph requires 0..100000 nodes and 0..500000 valid edges.");
        n=static_cast<std::size_t>(r.nodes);m=static_cast<std::size_t>(r.edges);
        graph=Directed(n);in.resize(n);out.resize(n);strength.resize(n);
        if(!n&&m)throw Failure(PENTA_NUMERIC_ARGUMENT,"A graph with edges requires nodes.");
        budget.check();budget.spend(r.nodes);
        std::set<std::pair<uint64_t,uint64_t>> unique;
        for(std::size_t id=0;id<m;++id){
            budget.spend();const auto a=r.endpoints[2*id],b=r.endpoints[2*id+1];
            if(a<1||b<1||a>n||b>n)throw Failure(PENTA_NUMERIC_ARGUMENT,"Graph node IDs must be integers in 1..nodes.");
            const double weight=r.weights?r.weights[id]:1;
            if(!std::isfinite(weight))throw Failure(PENTA_NUMERIC_ARGUMENT,"Graph weights must be finite real values.");
            std::pair<uint64_t,uint64_t> key{a,b};if(!r.directed&&key.second<key.first)std::swap(key.first,key.second);
            if(!r.multigraph&&!unique.emplace(key).second)throw Failure(PENTA_NUMERIC_ARGUMENT,"Duplicate edges require options(multigraph=1).");
            if(a==b)++selfLoops;
            if(weight<0)++negative;
            totalWeight+=weight;
            const auto u=static_cast<std::size_t>(a-1),v=static_cast<std::size_t>(b-1);
            boost::add_edge(u,v,Edge{weight,id+1},graph);
            ++out[u];++in[v];strength[u]+=weight;
            if(r.directed)strength[v]+=weight;
            else {
                ++out[v];++in[u];strength[v]+=weight;
                if(u!=v)boost::add_edge(v,u,Edge{weight,id+1},graph);
            }
        }
    }
    std::size_t start() const {if(request.source<1||request.source>n)throw Failure(PENTA_NUMERIC_ARGUMENT,"Graph source must be a node ID in 1..nodes.");return static_cast<std::size_t>(request.source-1);}
    std::size_t finish() const {if(request.target<1||request.target>n)throw Failure(PENTA_NUMERIC_ARGUMENT,"Graph target must be a node ID in 1..nodes.");return static_cast<std::size_t>(request.target-1);}
    std::pair<std::vector<double>,uint64_t> components(bool strong,Budget& budget) const {
        std::vector<int> raw(n,0);uint64_t count=0;
        if(n){
            budget.spend(n+2*m);
            if(strong&&request.directed)count=static_cast<uint64_t>(boost::strong_components(graph,raw.data()));
            else {Undirected weak(n);for(std::size_t id=0;id<m;++id)boost::add_edge(request.endpoints[2*id]-1,request.endpoints[2*id+1]-1,weak);count=static_cast<uint64_t>(boost::connected_components(weak,raw.data()));}
            budget.check();
        }
        // Canonical component labels follow the smallest node in each component.
        std::vector<double> labels(n);std::vector<uint64_t> ids(static_cast<std::size_t>(count),0);uint64_t next=0;
        for(std::size_t vertex=0;vertex<n;++vertex){auto& label=ids[static_cast<std::size_t>(raw[vertex])];if(!label)label=++next;labels[vertex]=static_cast<double>(label);}
        return {std::move(labels),count};
    }
};
struct Paths {
    std::vector<long double> distance;
    std::vector<std::size_t> predecessor;
    std::vector<uint64_t> edge;
    bool negativeCycle=false;
};
Paths paths(const Input& input,std::size_t source,bool bellman,bool weighted,Budget& budget) {
    const auto n=input.n;const long double infinity=std::numeric_limits<long double>::infinity();
    Paths result{std::vector<long double>(n,infinity),std::vector<std::size_t>(n,n),std::vector<uint64_t>(n,0),false};
    result.distance[source]=0;result.predecessor[source]=source;
    auto relax=[&](std::size_t u,Directed::edge_descriptor edge){
        budget.spend();const auto v=static_cast<std::size_t>(boost::target(edge,input.graph));
        const auto candidate=addDistance(result.distance[u],weighted?input.graph[edge].weight:1);
        if(candidate<result.distance[v]){result.distance[v]=candidate;result.predecessor[v]=u;result.edge[v]=input.graph[edge].id;return true;}return false;
    };
    if(bellman){
        for(std::size_t pass=1;pass<n;++pass){bool changed=false;for(std::size_t u=0;u<n;++u)if(std::isfinite(result.distance[u]))for(auto [edge,end]=boost::out_edges(u,input.graph);edge!=end;++edge)changed=relax(u,*edge)||changed;if(!changed)break;}
        for(std::size_t u=0;u<n;++u)if(std::isfinite(result.distance[u]))for(auto [edge,end]=boost::out_edges(u,input.graph);edge!=end;++edge){budget.spend();const auto v=static_cast<std::size_t>(boost::target(*edge,input.graph));if(addDistance(result.distance[u],input.graph[*edge].weight)<result.distance[v]){result.negativeCycle=true;return result;}}
    }else{
        using Candidate=std::pair<long double,std::size_t>;std::priority_queue<Candidate,std::vector<Candidate>,std::greater<Candidate>> queue;queue.emplace(0,source);
        while(!queue.empty()){const auto [value,u]=queue.top();queue.pop();budget.spend();if(value!=result.distance[u])continue;for(auto [edge,end]=boost::out_edges(u,input.graph);edge!=end;++edge)if(relax(u,*edge))queue.emplace(result.distance[boost::target(*edge,input.graph)],boost::target(*edge,input.graph));}
    }
    return result;
}
std::vector<double> centrality(const Input& input,uint32_t kind,bool normalized,bool weighted,Budget& budget) {
    const auto n=input.n;std::vector<long double> scores(n,0);
    if(kind<=3){
        for(std::size_t u=0;u<n;++u){budget.spend();scores[u]=kind==0?(input.request.directed?input.in[u]+input.out[u]:input.out[u]):kind==1?input.in[u]:kind==2?input.out[u]:input.strength[u];}
        if(normalized&&n>1)for(auto& score:scores)score/=n-1;
    }else{
        if(kind>6)throw Failure(PENTA_NUMERIC_ARGUMENT,"Unknown graph centrality kind.");
        if(weighted)for(std::size_t id=0;id<input.m;++id)if((input.request.weights?input.request.weights[id]:1)<=0)throw Failure(PENTA_NUMERIC_ARGUMENT,"Weighted path centralities require strictly positive weights; use weighted=0 for hop distances.");
        if(kind==5||kind==6){
            for(std::size_t source=0;source<n;++source){
                const auto result=paths(input,source,false,weighted,budget);long double sum=0,harmonic=0;std::size_t reachable=0;
                for(std::size_t target=0;target<n;++target)if(target!=source&&std::isfinite(result.distance[target])){++reachable;sum+=result.distance[target];harmonic+=1/result.distance[target];}
                if(kind==5)scores[source]=reachable&&sum>0?static_cast<long double>(reachable)/sum*(normalized&&n>1?static_cast<long double>(reachable)/(n-1):1):0;
                else scores[source]=normalized&&n>1?harmonic/(n-1):harmonic;
            }
        }else{
            const auto infinity=std::numeric_limits<long double>::infinity();
            for(std::size_t source=0;source<n;++source){
                std::vector<std::vector<std::size_t>> predecessors(n);std::vector<long double> distance(n,infinity),sigma(n,0),dependency(n,0);std::vector<unsigned char> settled(n,0);std::vector<std::size_t> order;order.reserve(n);
                using Candidate=std::pair<long double,std::size_t>;std::priority_queue<Candidate,std::vector<Candidate>,std::greater<Candidate>> queue;
                distance[source]=0;sigma[source]=1;queue.emplace(0,source);
                while(!queue.empty()){
                    const auto [d,u]=queue.top();queue.pop();budget.spend();if(d!=distance[u]||settled[u])continue;settled[u]=1;order.push_back(u);
                    for(auto [edge,end]=boost::out_edges(u,input.graph);edge!=end;++edge){
                        budget.spend();const auto v=static_cast<std::size_t>(boost::target(*edge,input.graph));const auto next=addDistance(d,weighted?input.graph[*edge].weight:1);
                        if(next<distance[v]){distance[v]=next;queue.emplace(next,v);sigma[v]=sigma[u];predecessors[v].assign(1,u);}
                        else if(next==distance[v]){sigma[v]+=sigma[u];if(!std::isfinite(sigma[v]))throw Failure(PENTA_NUMERIC_DOMAIN,"Graph shortest-path multiplicity exceeds the finite range.");predecessors[v].push_back(u);}
                    }
                }
                while(!order.empty()){const auto w=order.back();order.pop_back();for(const auto v:predecessors[w]){budget.spend();dependency[v]+=sigma[v]/sigma[w]*(1+dependency[w]);}if(w!=source)scores[w]+=dependency[w];}
            }
            if(!input.request.directed)for(auto& score:scores)score/=2;
            if(normalized&&n>2){const long double denominator=static_cast<long double>(n-1)*(n-2);for(auto& score:scores)score*=input.request.directed?1/denominator:2/denominator;}
        }
    }
    std::vector<double> output(n);for(std::size_t u=0;u<n;++u)output[u]=realResult(scores[u]);return output;
}
struct UnionFind {
    std::vector<std::size_t> parent,rank;
    explicit UnionFind(std::size_t n):parent(n),rank(n){std::iota(parent.begin(),parent.end(),0);}
    std::size_t root(std::size_t a){while(parent[a]!=a){parent[a]=parent[parent[a]];a=parent[a];}return a;}
    bool join(std::size_t a,std::size_t b){a=root(a);b=root(b);if(a==b)return false;if(rank[a]<rank[b])std::swap(a,b);parent[b]=a;if(rank[a]==rank[b])++rank[a];return true;}
};

int execute(const penta_graph_host_v1* host,const penta_graph_request_v1* request,penta_graph_result_v1* report,char* error,uint64_t capacity) noexcept {
    try {
        if(!host||host->struct_size<sizeof(*host)||host->abi_version!=PENTA_GRAPH_ABI_V1||!request||request->struct_size<sizeof(*request)||!report||report->struct_size<sizeof(*report))throw Failure(PENTA_NUMERIC_VERSION,"Incompatible graph module ABI.");
        if(!request->max_work)throw Failure(PENTA_GRAPH_WORK_LIMIT,"Graph native work allowance is zero.");
        Budget budget{*host,request->max_work};budget.check();Input input(*request,budget);
        std::vector<double> output;uint64_t rows=0,columns=1,extra[4]{};double scalar=0;uint32_t outcome=PENTA_GRAPH_COMPLETE;const char* status="complete";const char* method="native";
        switch(request->operation){
        case PENTA_GRAPH_INFO:{
            rows=input.n;columns=4;output.resize(input.n*4);
            for(std::size_t u=0;u<input.n;++u){output[4*u]=static_cast<double>(input.request.directed?input.in[u]+input.out[u]:input.out[u]);output[4*u+1]=static_cast<double>(input.in[u]);output[4*u+2]=static_cast<double>(input.out[u]);output[4*u+3]=realResult(input.strength[u]);}
            extra[0]=input.selfLoops;extra[1]=input.negative;extra[2]=input.components(false,budget).second;scalar=realResult(input.totalWeight);break;
        }
        case PENTA_GRAPH_COMPONENTS:{
            if(request->kind>1)throw Failure(PENTA_NUMERIC_ARGUMENT,"Components kind must be weak or strong.");
            auto result=input.components(request->kind==1,budget);output=std::move(result.first);rows=input.n;extra[0]=result.second;method=request->kind?"strong":"weak";break;
        }
        case PENTA_GRAPH_SHORTEST_PATH:{
            if(request->kind>2)throw Failure(PENTA_NUMERIC_ARGUMENT,"Shortest path method must be auto, dijkstra or bellman_ford.");
            const auto source=input.start(),target=input.finish();const bool bellman=request->kind==2||(request->kind==0&&input.negative);
            if(!bellman&&input.negative)throw Failure(PENTA_NUMERIC_ARGUMENT,"Dijkstra requires nonnegative graph weights.");
            method=bellman?"bellman_ford":"dijkstra";const auto result=paths(input,source,bellman,true,budget);
            if(result.negativeCycle){outcome=PENTA_GRAPH_NEGATIVE_CYCLE;status="negative_cycle";break;}
            if(!std::isfinite(result.distance[target])){outcome=PENTA_GRAPH_UNREACHABLE;status="unreachable";break;}
            scalar=realResult(result.distance[target]);std::vector<double> nodes,edges;std::size_t current=target;
            for(std::size_t step=0;;++step){if(step>input.n)throw Failure(PENTA_NUMERIC_INTERNAL,"Invalid shortest-path predecessor chain.");nodes.push_back(static_cast<double>(current+1));if(current==source)break;if(result.predecessor[current]>=input.n||!result.edge[current])throw Failure(PENTA_NUMERIC_INTERNAL,"Incomplete shortest-path predecessor chain.");edges.push_back(static_cast<double>(result.edge[current]));current=result.predecessor[current];}
            std::reverse(nodes.begin(),nodes.end());std::reverse(edges.begin(),edges.end());extra[0]=nodes.size();extra[1]=edges.size();rows=nodes.size();output=std::move(nodes);output.insert(output.end(),edges.begin(),edges.end());break;
        }
        case PENTA_GRAPH_SPANNING_TREE:{
            if(request->directed)throw Failure(PENTA_NUMERIC_ARGUMENT,"spanningtree requires an undirected graph.");
            std::vector<std::size_t> order(input.m);std::iota(order.begin(),order.end(),0);
            budget.spend(input.m*static_cast<uint64_t>(std::ceil(std::log2(input.m+1.0))));
            auto weight=[&](std::size_t id){return request->weights?request->weights[id]:1;};
            std::sort(order.begin(),order.end(),[&](auto a,auto b){const auto wa=weight(a),wb=weight(b);return wa<wb||(wa==wb&&a<b);});
            UnionFind sets(input.n);uint64_t components=input.n;long double total=0;
            for(const auto id:order){budget.spend();if(sets.join(request->endpoints[2*id]-1,request->endpoints[2*id+1]-1)){output.push_back(static_cast<double>(id+1));total+=weight(id);--components;}}
            rows=output.size();extra[0]=components;scalar=realResult(total);method="kruskal";if(components!=1){outcome=PENTA_GRAPH_FOREST;status="forest";}break;
        }
        case PENTA_GRAPH_TOPOSORT:{
            if(!request->directed)throw Failure(PENTA_NUMERIC_ARGUMENT,"toposort requires a directed graph.");
            std::vector<std::size_t> order;budget.spend(input.n+boost::num_edges(input.graph));
            try{boost::topological_sort(input.graph,std::back_inserter(order));budget.check();std::reverse(order.begin(),order.end());for(const auto vertex:order)output.push_back(static_cast<double>(vertex+1));rows=output.size();}
            catch(const boost::not_a_dag&){outcome=PENTA_GRAPH_CYCLIC;status="cyclic";}break;
        }
        case PENTA_GRAPH_CENTRALITY:
            if(request->normalized>1||request->reserved>1)throw Failure(PENTA_NUMERIC_ARGUMENT,"Centrality normalized and weighted flags must be0 or1.");
            rows=input.n;output=centrality(input,request->kind,request->normalized!=0,request->reserved!=0,budget);break;
        case PENTA_GRAPH_LAPLACIAN:case PENTA_GRAPH_ADJACENCY:{
            if(input.n&&input.n>report->output_capacity/input.n)throw Failure(PENTA_NUMERIC_ARGUMENT,"Dense graph matrix exceeds the requested output capacity.");
            rows=columns=input.n;output.assign(input.n*input.n,0);budget.spend(input.n*input.n);
            for(std::size_t id=0;id<input.m;++id){budget.spend();const auto u=request->endpoints[2*id]-1,v=request->endpoints[2*id+1]-1;const double w=request->weights?request->weights[id]:1;
                if(request->operation==PENTA_GRAPH_ADJACENCY){output[u*input.n+v]=realResult(static_cast<long double>(output[u*input.n+v])+w);if(!request->directed&&u!=v)output[v*input.n+u]=realResult(static_cast<long double>(output[v*input.n+u])+w);}
                else if(u!=v){output[u*input.n+u]=realResult(static_cast<long double>(output[u*input.n+u])+w);output[u*input.n+v]=realResult(static_cast<long double>(output[u*input.n+v])-w);if(!request->directed){output[v*input.n+v]=realResult(static_cast<long double>(output[v*input.n+v])+w);output[v*input.n+u]=realResult(static_cast<long double>(output[v*input.n+u])-w);}}
            }
            method=request->operation==PENTA_GRAPH_LAPLACIAN?"out_degree_minus_adjacency":"weighted_adjacency";break;
        }
        case PENTA_GRAPH_LAYOUT:{
            if(request->dimensions!=2&&request->dimensions!=3)throw Failure(PENTA_NUMERIC_ARGUMENT,"Graph layout dimensions must be2 or3.");
            if(!std::isfinite(request->radius)||request->radius<=0)throw Failure(PENTA_NUMERIC_ARGUMENT,"Graph layout radius must be positive and finite.");
            rows=input.n;columns=request->dimensions;output.assign(input.n*columns,0);const double pi=std::acos(-1.0);
            if(input.n>1){
                for(std::size_t vertex=0;vertex<input.n;++vertex){budget.spend();const double angle=2*pi*static_cast<double>(vertex)/static_cast<double>(input.n);output[vertex*columns]=request->radius*std::cos(angle);output[vertex*columns+1]=request->radius*std::sin(angle);}
            }
            method="circle";break;
        }
        case PENTA_GRAPH_CYCLE:{
            using Iterator=Directed::out_edge_iterator;struct Frame{std::size_t vertex;Iterator next,end;};
            std::vector<unsigned char> color(input.n,0);std::vector<std::size_t> parent(input.n,input.n);std::vector<uint64_t> edgeTo(input.n,0);std::vector<Frame> stack;
            bool found=false;std::vector<double> cycleEdges;
            for(std::size_t root=0;root<input.n&&!found;++root){if(color[root])continue;const auto range=boost::out_edges(root,input.graph);color[root]=1;stack.push_back({root,range.first,range.second});
                while(!stack.empty()&&!found){auto& frame=stack.back();if(frame.next==frame.end){color[frame.vertex]=2;stack.pop_back();continue;}const auto edge=*frame.next++;budget.spend();const auto u=frame.vertex,v=static_cast<std::size_t>(boost::target(edge,input.graph));const auto id=input.graph[edge].id;
                    if(!request->directed&&id==edgeTo[u])continue;
                    if(!color[v]){parent[v]=u;edgeTo[v]=id;color[v]=1;const auto next=boost::out_edges(v,input.graph);stack.push_back({v,next.first,next.second});}
                    else if(color[v]==1){std::size_t current=u;output.push_back(static_cast<double>(u+1));for(std::size_t count=0;current!=v;++count){if(count>input.n||parent[current]>=input.n)throw Failure(PENTA_NUMERIC_INTERNAL,"Invalid graph cycle chain.");cycleEdges.push_back(static_cast<double>(edgeTo[current]));current=parent[current];output.push_back(static_cast<double>(current+1));}std::reverse(output.begin(),output.end());std::reverse(cycleEdges.begin(),cycleEdges.end());output.push_back(static_cast<double>(v+1));cycleEdges.push_back(static_cast<double>(id));found=true;}
                }
            }
            rows=output.size();extra[0]=output.size();extra[1]=cycleEdges.size();extra[2]=found;output.insert(output.end(),cycleEdges.begin(),cycleEdges.end());status=found?"cycle":"acyclic";break;
        }
        default:throw Failure(PENTA_NUMERIC_ARGUMENT,"Unknown graph operation.");
        }
        budget.flush();
        if(output.size()>report->output_capacity||(!output.empty()&&!report->output))throw Failure(PENTA_NUMERIC_ARGUMENT,"Graph output capacity is insufficient.");
        for(const auto value:output)if(!std::isfinite(value))throw Failure(PENTA_NUMERIC_DOMAIN,"Nonfinite native graph output.");
        if(!output.empty())std::copy(output.begin(),output.end(),report->output);
        report->outcome=outcome;report->output_count=output.size();report->rows=rows;report->columns=columns;report->work_units=budget.used;report->scalar=scalar;std::copy(std::begin(extra),std::end(extra),std::begin(report->extra));message(report->status,sizeof(report->status),status);message(report->method,sizeof(report->method),method);return PENTA_NUMERIC_OK;
    }catch(const Failure& failure){message(error,capacity,failure.what());return failure.status;}
    catch(const std::exception& failure){message(error,capacity,failure.what());return PENTA_NUMERIC_INTERNAL;}
    catch(...){message(error,capacity,"Unknown native graph failure.");return PENTA_NUMERIC_INTERNAL;}
}
const penta_graph_api_v1 api{sizeof(penta_graph_api_v1),PENTA_GRAPH_ABI_V1,"penta_graph","BoostGraph-native-v1",execute};
}
extern "C" PENTA_NUMERIC_EXPORT const penta_graph_api_v1* penta_graph_query_v1(uint32_t version){return version==PENTA_GRAPH_ABI_V1?&api:nullptr;}
